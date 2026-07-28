#include "ZmodemTransfer.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <limits>

namespace {

constexpr int transferTimeoutMilliseconds = 15000;
constexpr int detectionFlushMilliseconds = 200;
constexpr int maximumRetries = 5;
constexpr int maximumProtocolErrors = 10;
constexpr int defaultUploadPacketsPerWindow = 8;

QString formatError(const QString &action, const QFileDevice &file) {
    return QStringLiteral("%1: %2").arg(action, file.errorString());
}

}// namespace

ZmodemTransfer::ZmodemTransfer(QObject *parent)
    : QObject(parent) {
    timeout_.setSingleShot(true);
    timeout_.setInterval(transferTimeoutMilliseconds);
    connect(&timeout_, &QTimer::timeout,
            this, &ZmodemTransfer::onTimeout);
    detectionFlushTimer_.setSingleShot(true);
    detectionFlushTimer_.setInterval(detectionFlushMilliseconds);
    connect(&detectionFlushTimer_, &QTimer::timeout,
            this, &ZmodemTransfer::flushPendingTerminalData);
}

QByteArray ZmodemTransfer::consume(const QByteArray &data) {
    detectionFlushTimer_.stop();
    parser_.addData(data);
    QByteArray terminalOutput;
    Zmodem::ParseItem item;
    while (parser_.next(&item)) {
        switch (item.kind) {
            case Zmodem::ParseItem::Kind::Header:
                if (!handleHeader(item.header)) {
                    terminalOutput.append(item.raw);
                }
                break;
            case Zmodem::ParseItem::Kind::Data:
                handleDataSubpacket(item);
                break;
            case Zmodem::ParseItem::Kind::PlainText:
                handlePlainText(item.data, &terminalOutput);
                break;
            case Zmodem::ParseItem::Kind::Cancel:
                if (state_ != State::Idle) {
                    handleRemoteCancel();
                }
                break;
            case Zmodem::ParseItem::Kind::Error:
                if (state_ == State::Idle) {
                    terminalOutput.append(item.raw);
                } else {
                    handleParseError(item.error);
                }
                break;
        }
    }
    if (state_ == State::Idle && parser_.hasPendingData()) {
        detectionFlushTimer_.start();
    }
    return terminalOutput;
}

bool ZmodemTransfer::isActive() const {
    return state_ != State::Idle;
}

ZmodemTransfer::Direction ZmodemTransfer::direction() const {
    return direction_;
}

void ZmodemTransfer::acceptDownload(const QString &directory) {
    if (state_ != State::PendingDownload) {
        return;
    }
    const QFileInfo directoryInfo(directory);
    if (!directoryInfo.isDir() || !directoryInfo.isWritable()) {
        failTransfer(tr("The selected download directory is not writable."),
                     true);
        return;
    }

    downloadDirectory_ = directoryInfo.absoluteFilePath();
    downloadedFileCount_ = 0;
    expectedDownloadFileCount_ = 0;
    state_ = State::DownloadWaitFile;
    sendReceiveInit();
}

void ZmodemTransfer::acceptUpload(const QStringList &filePaths) {
    if (state_ != State::PendingUpload) {
        return;
    }
    if (filePaths.isEmpty()) {
        cancelTransfer(true);
        return;
    }

    qint64 remainingBytes = 0;
    for (const QString &filePath: filePaths) {
        const QFileInfo fileInfo(filePath);
        if (!fileInfo.isFile() || !fileInfo.isReadable()) {
            failTransfer(tr("Cannot read file: %1")
                                 .arg(QDir::toNativeSeparators(filePath)),
                         true);
            return;
        }
        if (static_cast<quint64>(fileInfo.size()) > std::numeric_limits<quint32>::max()) {
            failTransfer(
                    tr("ZMODEM cannot transfer files larger than 4 GiB: %1")
                            .arg(QDir::toNativeSeparators(filePath)),
                    true);
            return;
        }
        if (fileInfo.size() > std::numeric_limits<qint64>::max() - remainingBytes) {
            failTransfer(tr("The selected files are too large."), true);
            return;
        }
        remainingBytes += fileInfo.size();
    }

    uploadPaths_ = filePaths;
    uploadIndex_ = 0;
    uploadedFileCount_ = 0;
    uploadRemainingBytes_ = remainingBytes;
    uploadUsesCrc32_ =
            (initialHeader_.data[3] & Zmodem::canFc32) != 0U;
    // Linux SSH sessions pass through PTYs and expect(1), so quote
    // terminal control characters even without an ESCCTL request.
    uploadEscapesControl_ = true;
    const int receiverBuffer =
            static_cast<int>(initialHeader_.data[0]) | (static_cast<int>(initialHeader_.data[1]) << 8);
    if (receiverBuffer == 0) {
        uploadChunkSize_ = 1024;
        uploadPacketsPerWindow_ =
                defaultUploadPacketsPerWindow;
    } else {
        uploadChunkSize_ =
                std::clamp(receiverBuffer, 1, 8192);
        uploadPacketsPerWindow_ = 1;
    }
    offerNextUpload();
}

void ZmodemTransfer::reject() {
    if (state_ == State::PendingDownload || state_ == State::PendingUpload) {
        cancelTransfer(true);
    }
}

void ZmodemTransfer::cancel() {
    if (state_ != State::Idle) {
        cancelTransfer(true);
    }
}

bool ZmodemTransfer::handleHeader(const Zmodem::Header &header) {
    if (state_ == State::Idle) {
        if (header.type == Zmodem::FrameType::Zrqinit) {
            initialHeader_ = header;
            direction_ = Direction::Download;
            state_ = State::PendingDownload;
            emit detected(direction_);
            return true;
        }
        if (header.type == Zmodem::FrameType::Zrinit) {
            initialHeader_ = header;
            direction_ = Direction::Upload;
            state_ = State::PendingUpload;
            emit detected(direction_);
            return true;
        }
        return false;
    }

    if (state_ == State::PendingDownload) {
        if (header.type == Zmodem::FrameType::Zrqinit) {
            initialHeader_ = header;
        }
        return true;
    }
    if (state_ == State::PendingUpload) {
        if (header.type == Zmodem::FrameType::Zrinit) {
            initialHeader_ = header;
        }
        return true;
    }

    if (header.type == Zmodem::FrameType::Zchallenge) {
        sendImmediate(Zmodem::encodeHexHeader(
                Zmodem::FrameType::Zack, header.position()));
        return true;
    }
    if (header.type == Zmodem::FrameType::Zabort || header.type == Zmodem::FrameType::Zcan) {
        handleRemoteCancel();
        return true;
    }

    switch (state_) {
        case State::DownloadWaitFile:
            switch (header.type) {
                case Zmodem::FrameType::Zrqinit:
                    sendReceiveInit();
                    break;
                case Zmodem::FrameType::Zsinit:
                    retryCount_ = 0;
                    timeout_.start();
                    state_ = State::DownloadWaitSinitData;
                    parser_.expectDataSubpacket();
                    break;
                case Zmodem::FrameType::Zfile:
                    retryCount_ = 0;
                    timeout_.start();
                    state_ = State::DownloadWaitFileInfo;
                    parser_.expectDataSubpacket();
                    break;
                case Zmodem::FrameType::Zfin:
                    state_ = State::DownloadWaitOverAndOut;
                    closingBytes_.clear();
                    sendRetryable(Zmodem::encodeHexHeader(
                            Zmodem::FrameType::Zfin));
                    break;
                default:
                    sendRetryable(Zmodem::encodeHexHeader(
                            Zmodem::FrameType::Znak));
                    break;
            }
            return true;

        case State::DownloadWaitDataHeader:
            if (header.type == Zmodem::FrameType::Zdata) {
                if (header.position() != static_cast<quint32>(downloadOffset_)) {
                    sendRetryable(Zmodem::encodeHexHeader(
                            Zmodem::FrameType::Zrpos,
                            static_cast<quint32>(downloadOffset_)));
                    return true;
                }
                state_ = State::DownloadData;
                parser_.expectDataSubpacket();
                waitWithRetry(Zmodem::encodeHexHeader(
                        Zmodem::FrameType::Zrpos,
                        static_cast<quint32>(downloadOffset_)));
            } else if (header.type == Zmodem::FrameType::Zfile) {
                closeDownloadFile();
                state_ = State::DownloadWaitFileInfo;
                parser_.expectDataSubpacket();
            } else {
                sendRetryable(Zmodem::encodeHexHeader(
                        Zmodem::FrameType::Zrpos,
                        static_cast<quint32>(downloadOffset_)));
            }
            return true;

        case State::DownloadWaitEof:
            if (header.type == Zmodem::FrameType::Zeof) {
                if (header.position() != static_cast<quint32>(downloadOffset_)) {
                    state_ = State::DownloadWaitDataHeader;
                    sendRetryable(Zmodem::encodeHexHeader(
                            Zmodem::FrameType::Zrpos,
                            static_cast<quint32>(downloadOffset_)));
                } else {
                    finishDownloadedFile();
                }
            } else if (header.type == Zmodem::FrameType::Zdata) {
                if (header.position() == static_cast<quint32>(downloadOffset_)) {
                    state_ = State::DownloadData;
                    parser_.expectDataSubpacket();
                    waitWithRetry(Zmodem::encodeHexHeader(
                            Zmodem::FrameType::Zrpos,
                            static_cast<quint32>(downloadOffset_)));
                } else {
                    state_ = State::DownloadWaitDataHeader;
                    sendRetryable(Zmodem::encodeHexHeader(
                            Zmodem::FrameType::Zrpos,
                            static_cast<quint32>(downloadOffset_)));
                }
            } else {
                state_ = State::DownloadWaitDataHeader;
                sendRetryable(Zmodem::encodeHexHeader(
                        Zmodem::FrameType::Zrpos,
                        static_cast<quint32>(downloadOffset_)));
            }
            return true;

        case State::DownloadData:
            if (header.type == Zmodem::FrameType::Zdata) {
                if (header.position() == static_cast<quint32>(downloadOffset_)) {
                    parser_.expectDataSubpacket();
                    waitWithRetry(Zmodem::encodeHexHeader(
                            Zmodem::FrameType::Zrpos,
                            static_cast<quint32>(downloadOffset_)));
                } else {
                    state_ = State::DownloadWaitDataHeader;
                    sendRetryable(Zmodem::encodeHexHeader(
                            Zmodem::FrameType::Zrpos,
                            static_cast<quint32>(downloadOffset_)));
                }
            } else {
                state_ = State::DownloadWaitDataHeader;
                sendRetryable(Zmodem::encodeHexHeader(
                        Zmodem::FrameType::Zrpos,
                        static_cast<quint32>(downloadOffset_)));
            }
            return true;

        case State::DownloadWaitOverAndOut:
            if (header.type == Zmodem::FrameType::Zfin) {
                sendRetryable(Zmodem::encodeHexHeader(
                        Zmodem::FrameType::Zfin));
            }
            return true;

        case State::UploadWaitFileResponse:
            switch (header.type) {
                case Zmodem::FrameType::Zrpos:
                    startUploadAt(header.position());
                    break;
                case Zmodem::FrameType::Zskip:
                    emit fileSkipped(direction_, uploadFileName_);
                    uploadRemainingBytes_ = std::max<qint64>(
                            0,
                            uploadRemainingBytes_ - uploadSize_);
                    closeUploadFile();
                    ++uploadIndex_;
                    offerNextUpload();
                    break;
                case Zmodem::FrameType::Zrinit:
                case Zmodem::FrameType::Znak:
                    if (!retryPacket_.isEmpty()) {
                        sendRetryable(retryPacket_);
                    }
                    break;
                default:
                    break;
            }
            return true;

        case State::UploadData:
            if (header.type == Zmodem::FrameType::Zrpos) {
                startUploadAt(header.position());
            }
            return true;

        case State::UploadWaitDataAck:
            if (header.type == Zmodem::FrameType::Zack) {
                if (header.position() > static_cast<quint32>(uploadOffset_)) {
                    failTransfer(
                            tr("The remote side acknowledged an invalid file offset."),
                            true);
                } else if (header.position() < static_cast<quint32>(uploadOffset_)) {
                    startUploadAt(header.position());
                } else {
                    uploadAcknowledgedOffset_ = uploadOffset_;
                    resetRetry();
                    state_ = State::UploadData;
                    QTimer::singleShot(
                            0, this, &ZmodemTransfer::pumpUpload);
                }
            } else if (header.type == Zmodem::FrameType::Zrpos) {
                startUploadAt(header.position());
            } else if (header.type == Zmodem::FrameType::Znak) {
                startUploadAt(
                        static_cast<quint32>(
                                uploadAcknowledgedOffset_));
            }
            return true;

        case State::UploadWaitFileDone:
            switch (header.type) {
                case Zmodem::FrameType::Zrinit:
                    finishUploadedFile();
                    break;
                case Zmodem::FrameType::Zrpos:
                    startUploadAt(header.position());
                    break;
                case Zmodem::FrameType::Zskip:
                    emit fileSkipped(direction_, uploadFileName_);
                    uploadRemainingBytes_ = std::max<qint64>(
                            0,
                            uploadRemainingBytes_ - uploadSize_);
                    closeUploadFile();
                    ++uploadIndex_;
                    offerNextUpload();
                    break;
                default:
                    break;
            }
            return true;

        case State::UploadWaitFinish:
            if (header.type == Zmodem::FrameType::Zfin) {
                sendImmediate(QByteArrayLiteral("OO"));
                completeTransfer();
            }
            return true;

        case State::DownloadWaitSinitData:
        case State::DownloadWaitFileInfo:
        case State::Idle:
        case State::PendingDownload:
        case State::PendingUpload:
            return true;
    }
    return true;
}

void ZmodemTransfer::handleDataSubpacket(
        const Zmodem::ParseItem &item) {
    resetRetry();

    if (state_ == State::DownloadWaitSinitData) {
        state_ = State::DownloadWaitFile;
        sendRetryable(Zmodem::encodeHexHeader(
                Zmodem::FrameType::Zack, 1));
        return;
    }

    if (state_ == State::DownloadWaitFileInfo) {
        if (item.frameEnd != Zmodem::FrameEnd::EndAck && item.frameEnd != Zmodem::FrameEnd::End) {
            handleParseError(tr("Invalid file information packet."));
            return;
        }
        handleFileInformation(item.data);
        return;
    }

    if (state_ != State::DownloadData || downloadFile_ == nullptr) {
        handleParseError(tr("Unexpected ZMODEM data packet."));
        return;
    }

    if (!item.data.isEmpty()) {
        if (downloadOffset_ > static_cast<qint64>(
                                      std::numeric_limits<quint32>::max()) -
                                      item.data.size()) {
            failTransfer(
                    tr("The downloaded file exceeds the ZMODEM size limit."),
                    true);
            return;
        }
        const qint64 written = downloadFile_->write(item.data);
        if (written != item.data.size()) {
            failTransfer(formatError(tr("Cannot write downloaded file"),
                                     *downloadFile_),
                         true);
            return;
        }
        downloadOffset_ += written;
    }

    const QByteArray retryPosition = Zmodem::encodeHexHeader(
            Zmodem::FrameType::Zrpos,
            static_cast<quint32>(downloadOffset_));
    switch (item.frameEnd) {
        case Zmodem::FrameEnd::Continue:
            parser_.expectDataSubpacket();
            waitWithRetry(retryPosition);
            break;
        case Zmodem::FrameEnd::ContinueAck:
            parser_.expectDataSubpacket();
            waitWithRetry(retryPosition);
            sendImmediate(Zmodem::encodeHexHeader(
                    Zmodem::FrameType::Zack,
                    static_cast<quint32>(downloadOffset_)));
            break;
        case Zmodem::FrameEnd::EndAck:
            state_ = State::DownloadWaitDataHeader;
            waitWithRetry(retryPosition);
            sendImmediate(Zmodem::encodeHexHeader(
                    Zmodem::FrameType::Zack,
                    static_cast<quint32>(downloadOffset_)));
            break;
        case Zmodem::FrameEnd::End:
            state_ = State::DownloadWaitEof;
            waitWithRetry(retryPosition);
            break;
    }
    emit fileProgress(direction_,
                      downloadFileName_,
                      downloadOffset_,
                      downloadSize_);
}

void ZmodemTransfer::handleParseError(const QString &message) {
    ++protocolErrorCount_;
    if (protocolErrorCount_ > maximumProtocolErrors) {
        failTransfer(message, true);
        return;
    }

    if (state_ == State::DownloadWaitSinitData || state_ == State::DownloadWaitFileInfo) {
        state_ = State::DownloadWaitFile;
        sendRetryable(Zmodem::encodeHexHeader(
                Zmodem::FrameType::Znak));
    } else if (direction_ == Direction::Download && downloadFile_ != nullptr) {
        state_ = State::DownloadWaitDataHeader;
        sendRetryable(Zmodem::encodeHexHeader(
                Zmodem::FrameType::Zrpos,
                static_cast<quint32>(downloadOffset_)));
    } else {
        sendRetryable(Zmodem::encodeHexHeader(
                Zmodem::FrameType::Znak));
    }
}

void ZmodemTransfer::handlePlainText(
        const QByteArray &data,
        QByteArray *terminalOutput) {
    if (state_ == State::Idle) {
        terminalOutput->append(data);
        return;
    }
    if (state_ != State::DownloadWaitOverAndOut) {
        return;
    }

    closingBytes_.append(data);
    const qsizetype overAndOut = closingBytes_.indexOf("OO");
    if (overAndOut < 0) {
        if (closingBytes_.size() > 64) {
            failTransfer(tr("Invalid ZMODEM session ending."), true);
        }
        return;
    }

    const QByteArray trailing = closingBytes_.mid(overAndOut + 2);
    closingBytes_.clear();
    completeTransfer();
    terminalOutput->append(trailing);
}

void ZmodemTransfer::handleRemoteCancel() {
    const Direction canceledDirection = direction_;
    resetTransfer(false);
    emit transferFailed(
            canceledDirection,
            tr("The remote side canceled the ZMODEM transfer."));
}

void ZmodemTransfer::sendImmediate(const QByteArray &data) {
    if (!data.isEmpty()) {
        emit outboundData(data);
    }
}

void ZmodemTransfer::sendRetryable(const QByteArray &data) {
    retryPacket_ = data;
    retryCount_ = 0;
    timeout_.start();
    sendImmediate(data);
}

void ZmodemTransfer::waitWithRetry(const QByteArray &data) {
    retryPacket_ = data;
    retryCount_ = 0;
    timeout_.start();
}

void ZmodemTransfer::resetRetry() {
    timeout_.stop();
    retryCount_ = 0;
}

void ZmodemTransfer::sendReceiveInit() {
    std::array<quint8, 4> data{
            0x00U,
            0x04U,
            0x00U,
            static_cast<quint8>(
                    Zmodem::canFdx | Zmodem::canOvio | Zmodem::canFc32 | Zmodem::escapeCtl)};
    sendRetryable(Zmodem::encodeHexHeader(
            Zmodem::FrameType::Zrinit, data));
}

void ZmodemTransfer::handleFileInformation(const QByteArray &data) {
    const qsizetype nameEnd = data.indexOf('\0');
    if (nameEnd <= 0) {
        handleParseError(tr("The remote side sent an invalid file name."));
        return;
    }

    const QByteArray remoteName = data.left(nameEnd);
    const QByteArray properties =
            data.mid(nameEnd + 1).split('\0').value(0);
    const QList<QByteArray> fields = properties.split(' ');
    bool sizeOk = false;
    const qint64 size = fields.value(0).toLongLong(&sizeOk, 10);
    downloadSize_ = sizeOk && size >= 0 ? size : -1;
    if (downloadSize_ > std::numeric_limits<quint32>::max()) {
        failTransfer(
                tr("ZMODEM cannot receive files larger than 4 GiB."),
                true);
        return;
    }

    bool modificationTimeOk = false;
    const qint64 modificationTime =
            fields.value(1).toLongLong(&modificationTimeOk, 8);
    downloadModificationTime_ =
            modificationTimeOk ? modificationTime : -1;

    bool fileCountOk = false;
    const int fileCount = fields.value(4).toInt(&fileCountOk, 10);
    if (fileCountOk && fileCount > 0) {
        expectedDownloadFileCount_ =
                std::max(expectedDownloadFileCount_,
                         downloadedFileCount_ + fileCount);
    }

    downloadFileName_ = safeDownloadName(remoteName);
    if (downloadFileName_.isEmpty()) {
        state_ = State::DownloadWaitFile;
        emit fileSkipped(direction_, QString::fromUtf8(remoteName));
        sendRetryable(Zmodem::encodeHexHeader(
                Zmodem::FrameType::Zskip));
        return;
    }

    downloadFilePath_ = uniqueDownloadPath(downloadFileName_);
    downloadFile_ = std::make_unique<QSaveFile>(downloadFilePath_);
    if (!downloadFile_->open(QIODevice::WriteOnly)) {
        failTransfer(formatError(tr("Cannot create download file"),
                                 *downloadFile_),
                     true);
        return;
    }

    downloadOffset_ = 0;
    const int shownFileCount =
            std::max(expectedDownloadFileCount_,
                     downloadedFileCount_ + 1);
    state_ = State::DownloadWaitDataHeader;
    sendRetryable(Zmodem::encodeHexHeader(
            Zmodem::FrameType::Zrpos, 0));
    emit fileStarted(direction_,
                     downloadFileName_,
                     downloadSize_,
                     downloadedFileCount_ + 1,
                     shownFileCount);
}

QString ZmodemTransfer::safeDownloadName(
        const QByteArray &remoteName) {
    QString normalizedName = QString::fromUtf8(remoteName);
    normalizedName.replace('\\', '/');
    QString fileName =
            normalizedName.section('/', -1, -1).trimmed();
    if (fileName.isEmpty() || fileName == QStringLiteral(".") || fileName == QStringLiteral("..")) {
        return {};
    }
    return fileName;
}

QString ZmodemTransfer::uniqueDownloadPath(
        const QString &fileName) const {
    QDir directory(downloadDirectory_);
    QString candidate = directory.filePath(fileName);
    if (!QFileInfo::exists(candidate)) {
        return candidate;
    }

    const QFileInfo fileInfo(fileName);
    QString baseName = fileInfo.completeBaseName();
    const QString suffix = fileInfo.completeSuffix();
    if (baseName.isEmpty()) {
        baseName = fileName;
    }
    for (int number = 1;; ++number) {
        const QString numberedName =
                suffix.isEmpty()
                        ? QStringLiteral("%1 (%2)")
                                  .arg(baseName)
                                  .arg(number)
                        : QStringLiteral("%1 (%2).%3")
                                  .arg(baseName)
                                  .arg(number)
                                  .arg(suffix);
        candidate = directory.filePath(numberedName);
        if (!QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
}

void ZmodemTransfer::finishDownloadedFile() {
    if (downloadFile_ == nullptr) {
        failTransfer(tr("No download file is open."), true);
        return;
    }
    if (downloadSize_ >= 0 && downloadOffset_ != downloadSize_) {
        state_ = State::DownloadWaitDataHeader;
        sendRetryable(Zmodem::encodeHexHeader(
                Zmodem::FrameType::Zrpos,
                static_cast<quint32>(downloadOffset_)));
        return;
    }
    if (!downloadFile_->commit()) {
        failTransfer(formatError(tr("Cannot save downloaded file"),
                                 *downloadFile_),
                     true);
        return;
    }

    if (downloadModificationTime_ >= 0) {
        QFile savedFile(downloadFilePath_);
        if (savedFile.open(QIODevice::ReadWrite)) {
            savedFile.setFileTime(
                    QDateTime::fromSecsSinceEpoch(
                            downloadModificationTime_),
                    QFileDevice::FileModificationTime);
        }
    }

    emit fileProgress(direction_,
                      downloadFileName_,
                      downloadOffset_,
                      downloadSize_);
    emit fileFinished(direction_, downloadFilePath_);
    ++downloadedFileCount_;
    downloadFile_.reset();
    downloadFileName_.clear();
    downloadFilePath_.clear();
    downloadOffset_ = 0;
    downloadSize_ = -1;
    downloadModificationTime_ = -1;
    state_ = State::DownloadWaitFile;
    sendReceiveInit();
}

void ZmodemTransfer::closeDownloadFile() {
    if (downloadFile_ != nullptr) {
        downloadFile_->cancelWriting();
        downloadFile_.reset();
    }
    downloadFileName_.clear();
    downloadFilePath_.clear();
    downloadOffset_ = 0;
    downloadSize_ = -1;
    downloadModificationTime_ = -1;
}

void ZmodemTransfer::offerNextUpload() {
    closeUploadFile();
    if (uploadIndex_ >= uploadPaths_.size()) {
        state_ = State::UploadWaitFinish;
        sendRetryable(Zmodem::encodeHexHeader(
                Zmodem::FrameType::Zfin));
        return;
    }

    uploadFile_.setFileName(uploadPaths_.at(uploadIndex_));
    if (!uploadFile_.open(QIODevice::ReadOnly)) {
        failTransfer(formatError(tr("Cannot open upload file"),
                                 uploadFile_),
                     true);
        return;
    }

    const QFileInfo fileInfo(uploadFile_);
    uploadFileName_ = fileInfo.fileName();
    uploadSize_ = fileInfo.size();
    uploadOffset_ = 0;
    std::array<quint8, 4> fileFlags{
            0x00U, 0x00U, 0x00U, Zmodem::binaryFile};
    QByteArray offer = Zmodem::encodeBinaryHeader(
            Zmodem::FrameType::Zfile,
            fileFlags,
            uploadUsesCrc32_,
            uploadEscapesControl_);
    offer.append(Zmodem::encodeDataSubpacket(
            uploadFileInformation(),
            Zmodem::FrameEnd::EndAck,
            uploadUsesCrc32_,
            uploadEscapesControl_));

    state_ = State::UploadWaitFileResponse;
    sendRetryable(offer);
    emit fileStarted(direction_,
                     uploadFileName_,
                     uploadSize_,
                     uploadIndex_ + 1,
                     static_cast<int>(uploadPaths_.size()));
}

QByteArray ZmodemTransfer::uploadFileInformation() const {
    const QFileInfo fileInfo(uploadFile_);
    QByteArray information = uploadFileName_.toUtf8();
    information.append('\0');

    const qint64 modificationTime =
            fileInfo.lastModified().toSecsSinceEpoch();
    const int filesRemaining =
            static_cast<int>(uploadPaths_.size()) - uploadIndex_;
    const QString properties =
            QStringLiteral("%1 %2 100644 0 %3 %4")
                    .arg(uploadSize_)
                    .arg(QString::number(modificationTime, 8))
                    .arg(filesRemaining)
                    .arg(uploadRemainingBytes_);
    information.append(properties.toLatin1());
    information.append('\0');
    return information;
}

void ZmodemTransfer::startUploadAt(quint32 position) {
    if (!uploadFile_.isOpen() || static_cast<quint64>(position) > static_cast<quint64>(uploadSize_)) {
        failTransfer(tr("The remote side requested an invalid file offset."),
                     true);
        return;
    }
    if (!uploadFile_.seek(position)) {
        failTransfer(formatError(tr("Cannot seek in upload file"),
                                 uploadFile_),
                     true);
        return;
    }

    uploadOffset_ = position;
    uploadAcknowledgedOffset_ = position;
    ++uploadGeneration_;
    state_ = State::UploadData;
    resetRetry();
    sendImmediate(Zmodem::encodeBinaryHeader(
            Zmodem::FrameType::Zdata,
            position,
            uploadUsesCrc32_,
            uploadEscapesControl_));
    emit fileProgress(direction_,
                      uploadFileName_,
                      uploadOffset_,
                      uploadSize_);
    QTimer::singleShot(0, this, &ZmodemTransfer::pumpUpload);
}

void ZmodemTransfer::finishUploadedFile() {
    uploadRemainingBytes_ -= uploadSize_;
    uploadRemainingBytes_ = std::max<qint64>(0, uploadRemainingBytes_);
    emit fileProgress(direction_,
                      uploadFileName_,
                      uploadSize_,
                      uploadSize_);
    emit fileFinished(direction_, uploadFile_.fileName());
    ++uploadedFileCount_;
    closeUploadFile();
    ++uploadIndex_;
    offerNextUpload();
}

void ZmodemTransfer::closeUploadFile() {
    ++uploadGeneration_;
    if (uploadFile_.isOpen()) {
        uploadFile_.close();
    }
    uploadFileName_.clear();
    uploadOffset_ = 0;
    uploadAcknowledgedOffset_ = 0;
    uploadSize_ = 0;
}

void ZmodemTransfer::completeTransfer() {
    const Direction completedDirection = direction_;
    const int completedFiles =
            completedDirection == Direction::Download
                    ? downloadedFileCount_
                    : uploadedFileCount_;
    resetTransfer();
    emit transferFinished(completedDirection, completedFiles);
}

void ZmodemTransfer::failTransfer(const QString &message,
                                  bool notifyPeer) {
    const Direction failedDirection = direction_;
    if (notifyPeer) {
        sendImmediate(Zmodem::abortSequence());
    }
    resetTransfer();
    emit transferFailed(failedDirection, message);
}

void ZmodemTransfer::cancelTransfer(bool notifyPeer) {
    const Direction canceledDirection = direction_;
    if (notifyPeer) {
        sendImmediate(Zmodem::abortSequence());
    }
    resetTransfer();
    emit transferCanceled(canceledDirection);
}

void ZmodemTransfer::resetTransfer(bool resetParser) {
    timeout_.stop();
    detectionFlushTimer_.stop();
    retryPacket_.clear();
    retryCount_ = 0;
    protocolErrorCount_ = 0;
    closeDownloadFile();
    closeUploadFile();
    uploadPaths_.clear();
    uploadIndex_ = 0;
    uploadRemainingBytes_ = 0;
    downloadedFileCount_ = 0;
    expectedDownloadFileCount_ = 0;
    uploadedFileCount_ = 0;
    downloadDirectory_.clear();
    closingBytes_.clear();
    if (resetParser) {
        parser_.reset();
    }
    state_ = State::Idle;
}

void ZmodemTransfer::flushPendingTerminalData() {
    if (state_ != State::Idle) {
        return;
    }
    const QByteArray data = parser_.takePendingData();
    if (!data.isEmpty()) {
        emit terminalDataReady(data);
    }
}

void ZmodemTransfer::onTimeout() {
    if (state_ == State::Idle || state_ == State::PendingDownload || state_ == State::PendingUpload || state_ == State::UploadData) {
        return;
    }
    if (state_ == State::UploadWaitDataAck) {
        if (retryCount_ >= maximumRetries) {
            failTransfer(tr("The ZMODEM peer did not acknowledge file data."),
                         true);
            return;
        }
        if (!uploadFile_.seek(uploadAcknowledgedOffset_)) {
            failTransfer(formatError(tr("Cannot seek in upload file"),
                                     uploadFile_),
                         true);
            return;
        }
        ++retryCount_;
        uploadOffset_ = uploadAcknowledgedOffset_;
        ++uploadGeneration_;
        state_ = State::UploadData;
        sendImmediate(Zmodem::encodeBinaryHeader(
                Zmodem::FrameType::Zdata,
                static_cast<quint32>(uploadOffset_),
                uploadUsesCrc32_,
                uploadEscapesControl_));
        QTimer::singleShot(0, this, &ZmodemTransfer::pumpUpload);
        return;
    }
    if (retryPacket_.isEmpty()) {
        failTransfer(tr("The ZMODEM transfer timed out."), true);
        return;
    }
    if (retryCount_ >= maximumRetries) {
        failTransfer(tr("The ZMODEM peer did not respond."), true);
        return;
    }

    if (state_ == State::DownloadData || state_ == State::DownloadWaitSinitData || state_ == State::DownloadWaitFileInfo) {
        parser_.expectHeader();
        state_ =
                downloadFile_ == nullptr
                        ? State::DownloadWaitFile
                        : State::DownloadWaitDataHeader;
    }
    ++retryCount_;
    sendImmediate(retryPacket_);
    timeout_.start();
}

void ZmodemTransfer::pumpUpload() {
    if (state_ != State::UploadData || !uploadFile_.isOpen()) {
        return;
    }
    const quint64 generation = uploadGeneration_;

    for (int packet = 0;
         packet < uploadPacketsPerWindow_;
         ++packet) {
        QByteArray chunk = uploadFile_.read(uploadChunkSize_);
        if (chunk.isEmpty() && uploadFile_.error() != QFileDevice::NoError) {
            failTransfer(formatError(tr("Cannot read upload file"),
                                     uploadFile_),
                         true);
            return;
        }

        const bool isLastChunk =
                uploadFile_.pos() >= uploadSize_;
        const bool waitForAcknowledgment =
                !isLastChunk && packet == uploadPacketsPerWindow_ - 1;
        const QByteArray dataPacket = Zmodem::encodeDataSubpacket(
                chunk,
                isLastChunk
                        ? Zmodem::FrameEnd::End
                : waitForAcknowledgment
                        ? Zmodem::FrameEnd::ContinueAck
                        : Zmodem::FrameEnd::Continue,
                uploadUsesCrc32_,
                uploadEscapesControl_);
        uploadOffset_ += chunk.size();

        if (isLastChunk) {
            const QByteArray eofHeader =
                    Zmodem::encodeBinaryHeader(
                            Zmodem::FrameType::Zeof,
                            static_cast<quint32>(uploadOffset_),
                            uploadUsesCrc32_,
                            uploadEscapesControl_);
            state_ = State::UploadWaitFileDone;
            retryPacket_ = eofHeader;
            retryCount_ = 0;
            timeout_.start();
            sendImmediate(dataPacket);
            if (generation != uploadGeneration_ || state_ != State::UploadWaitFileDone) {
                return;
            }
            sendImmediate(eofHeader);
            if (generation != uploadGeneration_ || state_ != State::UploadWaitFileDone) {
                return;
            }
            emit fileProgress(direction_,
                              uploadFileName_,
                              uploadOffset_,
                              uploadSize_);
            return;
        }
        if (waitForAcknowledgment) {
            state_ = State::UploadWaitDataAck;
            retryPacket_.clear();
            timeout_.start();
            sendImmediate(dataPacket);
            if (generation != uploadGeneration_ || state_ != State::UploadWaitDataAck) {
                return;
            }
            emit fileProgress(direction_,
                              uploadFileName_,
                              uploadOffset_,
                              uploadSize_);
            return;
        }
        sendImmediate(dataPacket);
        if (generation != uploadGeneration_ || state_ != State::UploadData) {
            return;
        }
        emit fileProgress(direction_,
                          uploadFileName_,
                          uploadOffset_,
                          uploadSize_);
        if (generation != uploadGeneration_ || state_ != State::UploadData) {
            return;
        }
    }
}
