#include "XyModemTransfer.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <limits>

namespace {

constexpr int transferTimeoutMilliseconds = 10000;
constexpr int maximumRetries = 10;
constexpr int maximumProtocolErrors = 10;
constexpr int crcRequestsBeforeChecksumFallback = 3;

QString formatError(const QString &action,
                    const QFileDevice &file) {
    return QStringLiteral("%1: %2")
            .arg(action, file.errorString());
}

QByteArray makeMetadata(const QFileInfo &fileInfo,
                        const int filesLeft,
                        const qint64 bytesLeft) {
    QByteArray metadata = fileInfo.fileName().toUtf8();
    metadata.append('\0');
    metadata.append(QByteArray::number(fileInfo.size()));
    metadata.append(' ');
    metadata.append(QByteArray::number(
            fileInfo.lastModified().toSecsSinceEpoch(), 8));
    metadata.append(" 100644 0 ");
    metadata.append(QByteArray::number(filesLeft));
    metadata.append(' ');
    metadata.append(QByteArray::number(bytesLeft));
    metadata.append('\0');
    return metadata;
}

}// namespace

XyModemTransfer::XyModemTransfer(QObject *parent)
    : QObject(parent) {
    timeout_.setSingleShot(true);
    timeout_.setInterval(transferTimeoutMilliseconds);
    connect(&timeout_, &QTimer::timeout,
            this, &XyModemTransfer::onTimeout);
}

QByteArray XyModemTransfer::consume(const QByteArray &data) {
    if (state_ == State::Idle) {
        return data;
    }

    inputBuffer_.append(data);
    QByteArray terminalOutput;
    if (direction_ == Direction::Upload) {
        processSenderInput(&terminalOutput);
    } else {
        processReceiverInput(&terminalOutput);
    }
    if (state_ == State::Idle && !inputBuffer_.isEmpty()) {
        terminalOutput.append(inputBuffer_);
        inputBuffer_.clear();
    }
    return terminalOutput;
}

bool XyModemTransfer::isActive() const {
    return state_ != State::Idle;
}

XyModemTransfer::Protocol XyModemTransfer::protocol() const {
    return protocol_;
}

XyModemTransfer::Direction XyModemTransfer::direction() const {
    return direction_;
}

void XyModemTransfer::send(const Protocol protocol,
                           const QStringList &filePaths) {
    if (isActive()) {
        emit transferFailed(
                protocol,
                Direction::Upload,
                tr("Another X/YMODEM transfer is already active."));
        return;
    }

    QString errorMessage;
    if (!validateUploadPaths(protocol, filePaths,
                             &errorMessage)) {
        emit transferFailed(protocol, Direction::Upload,
                            errorMessage);
        return;
    }

    inputBuffer_.clear();
    protocol_ = protocol;
    direction_ = Direction::Upload;
    uploadPaths_ = filePaths;
    uploadIndex_ = 0;
    uploadedFileCount_ = 0;
    state_ = State::SendWaitStart;
    retryCount_ = 0;
    protocolErrorCount_ = 0;
    retryPacket_.clear();
    timeout_.start();
}

void XyModemTransfer::receive(const Protocol protocol,
                              const QString &destination) {
    if (isActive()) {
        emit transferFailed(
                protocol,
                Direction::Download,
                tr("Another X/YMODEM transfer is already active."));
        return;
    }

    inputBuffer_.clear();
    protocol_ = protocol;
    direction_ = Direction::Download;
    checkMode_ = XyModem::CheckMode::Crc16;
    retryCount_ = 0;
    protocolErrorCount_ = 0;
    downloadedFileCount_ = 0;
    expectedDownloadFileCount_ = 0;
    expectedDownloadBlock_ = 1;
    initialCrcRequestCount_ = 1;
    downloadDestination_ = destination;

    if (protocol == Protocol::Xmodem) {
        const QFileInfo targetInfo(destination);
        const QFileInfo directoryInfo(
                targetInfo.absoluteDir().absolutePath());
        if (targetInfo.fileName().isEmpty() || !directoryInfo.isDir() || !directoryInfo.isWritable()) {
            resetTransfer();
            emit transferFailed(
                    protocol,
                    Direction::Download,
                    tr("The selected XMODEM destination is not writable."));
            return;
        }

        downloadFileName_ = targetInfo.fileName();
        downloadFilePath_ = targetInfo.absoluteFilePath();
        downloadFile_ =
                std::make_unique<QSaveFile>(downloadFilePath_);
        if (!downloadFile_->open(QIODevice::WriteOnly)) {
            const QString message = formatError(
                    tr("Cannot create download file"),
                    *downloadFile_);
            resetTransfer();
            emit transferFailed(protocol,
                                Direction::Download,
                                message);
            return;
        }

        downloadSize_ = -1;
        downloadOffset_ = 0;
        state_ = State::ReceiveWaitData;
        emit fileStarted(protocol_,
                         direction_,
                         downloadFileName_,
                         downloadSize_,
                         1,
                         1);
    } else {
        const QFileInfo directoryInfo(destination);
        if (!directoryInfo.isDir() || !directoryInfo.isWritable()) {
            resetTransfer();
            emit transferFailed(
                    protocol,
                    Direction::Download,
                    tr("The selected YMODEM download directory is not writable."));
            return;
        }
        downloadDestination_ =
                directoryInfo.absoluteFilePath();
        state_ = State::ReceiveWaitMetadata;
    }

    respond(QByteArray(1, XyModem::crcRequest));
}

void XyModemTransfer::cancel() {
    if (!isActive()) {
        return;
    }
    const Protocol canceledProtocol = protocol_;
    const Direction canceledDirection = direction_;
    emit outboundData(XyModem::cancelSequence());
    resetTransfer();
    inputBuffer_.clear();
    emit transferCanceled(canceledProtocol,
                          canceledDirection);
}

void XyModemTransfer::processSenderInput(
        QByteArray *terminalOutput) {
    while (isActive() && !inputBuffer_.isEmpty()) {
        const char value = inputBuffer_.at(0);
        inputBuffer_.remove(0, 1);
        if (value == XyModem::can) {
            while (!inputBuffer_.isEmpty() && (inputBuffer_.at(0) == XyModem::can || inputBuffer_.at(0) == '\b')) {
                inputBuffer_.remove(0, 1);
            }
            handleRemoteCancel();
            return;
        }
        if (value == XyModem::ack || value == XyModem::nak || value == XyModem::crcRequest) {
            handleSenderControl(value);
            continue;
        }
        terminalOutput->append(value);
    }
}

void XyModemTransfer::handleSenderControl(
        const char value) {
    switch (state_) {
        case State::SendWaitStart:
            if (protocol_ == Protocol::Xmodem && (value == XyModem::crcRequest || value == XyModem::nak)) {
                checkMode_ =
                        value == XyModem::crcRequest
                                ? XyModem::CheckMode::Crc16
                                : XyModem::CheckMode::Checksum;
                resetRetry();
                offerCurrentUpload();
            } else if (protocol_ == Protocol::Ymodem && value == XyModem::crcRequest) {
                checkMode_ = XyModem::CheckMode::Crc16;
                resetRetry();
                offerCurrentUpload();
            }
            break;
        case State::SendWaitMetadataAck:
            if (value == XyModem::ack) {
                retryCount_ = 0;
                state_ = State::SendWaitMetadataRequest;
                timeout_.start();
            } else if (value == XyModem::nak) {
                retry(retryPacket_);
            }
            break;
        case State::SendWaitMetadataRequest:
            if (value == XyModem::crcRequest) {
                resetRetry();
                sendNextUploadBlock();
            } else if (value == XyModem::nak) {
                retry(retryPacket_);
            }
            break;
        case State::SendWaitDataAck:
            if (value == XyModem::ack) {
                uploadOffset_ += uploadPacketBytes_;
                emit fileProgress(protocol_,
                                  direction_,
                                  uploadFileName_,
                                  uploadOffset_,
                                  uploadSize_);
                ++uploadBlockNumber_;
                resetRetry();
                sendNextUploadBlock();
            } else if (value == XyModem::nak || value == XyModem::crcRequest) {
                retry(retryPacket_);
            }
            break;
        case State::SendWaitFirstEotResponse:
            if (protocol_ == Protocol::Xmodem && value == XyModem::ack) {
                resetRetry();
                finishUploadedFile();
                completeTransfer();
            } else if (protocol_ == Protocol::Ymodem && value == XyModem::nak) {
                transmit(QByteArray(1, XyModem::eot),
                         State::SendWaitFinalEotAck);
            } else if (protocol_ == Protocol::Ymodem && (value == XyModem::ack || value == XyModem::crcRequest)) {
                resetRetry();
                finishUploadedFile();
                state_ = State::SendWaitNextFileRequest;
                timeout_.start();
                if (value == XyModem::crcRequest) {
                    offerCurrentUpload();
                }
            } else if (value == XyModem::nak) {
                retry(retryPacket_);
            }
            break;
        case State::SendWaitFinalEotAck:
            if (value == XyModem::ack || value == XyModem::crcRequest) {
                resetRetry();
                finishUploadedFile();
                state_ = State::SendWaitNextFileRequest;
                timeout_.start();
                if (value == XyModem::crcRequest) {
                    offerCurrentUpload();
                }
            } else if (value == XyModem::nak) {
                retry(retryPacket_);
            }
            break;
        case State::SendWaitNextFileRequest:
            if (value == XyModem::crcRequest) {
                resetRetry();
                offerCurrentUpload();
            }
            break;
        case State::SendWaitBatchEndAck:
            if (value == XyModem::ack) {
                resetRetry();
                completeTransfer();
            } else if (value == XyModem::nak || value == XyModem::crcRequest) {
                retry(retryPacket_);
            }
            break;
        default:
            break;
    }
}

void XyModemTransfer::processReceiverInput(
        QByteArray *terminalOutput) {
    while (isActive() && !inputBuffer_.isEmpty()) {
        const char value = inputBuffer_.at(0);
        if (value == XyModem::soh || value == XyModem::stx) {
            const qsizetype packetSize =
                    XyModem::encodedPacketSize(
                            value, checkMode_);
            if (inputBuffer_.size() < packetSize) {
                timeout_.start();
                return;
            }

            const QByteArray encoded =
                    inputBuffer_.left(packetSize);
            inputBuffer_.remove(0, packetSize);
            XyModem::Packet packet;
            if (!XyModem::decodePacket(
                        encoded, checkMode_, &packet)) {
                ++protocolErrorCount_;
                if (protocolErrorCount_ > maximumProtocolErrors) {
                    failTransfer(
                            tr("Too many invalid X/YMODEM packets."),
                            true);
                    return;
                }
                retry(QByteArray(1, XyModem::nak));
                continue;
            }
            handleReceivedPacket(packet);
            continue;
        }

        inputBuffer_.remove(0, 1);
        if (value == XyModem::can) {
            while (!inputBuffer_.isEmpty() && (inputBuffer_.at(0) == XyModem::can || inputBuffer_.at(0) == '\b')) {
                inputBuffer_.remove(0, 1);
            }
            handleRemoteCancel();
            return;
        }
        if (value == XyModem::eot) {
            handleReceiveEot();
            continue;
        }
        if (value != XyModem::ack && value != XyModem::nak && value != XyModem::crcRequest) {
            terminalOutput->append(value);
        }
    }
}

void XyModemTransfer::handleReceivedPacket(
        const XyModem::Packet &packet) {
    if (state_ == State::ReceiveWaitMetadata) {
        if (packet.number != 0) {
            retry(QByteArray(1, XyModem::nak));
            return;
        }
        if (!packet.data.isEmpty() && packet.data.at(0) == '\0') {
            emit outboundData(QByteArray(1, XyModem::ack));
            completeTransfer();
            return;
        }
        handleDownloadMetadata(packet.data);
        return;
    }

    if (state_ != State::ReceiveWaitData && state_ != State::ReceiveWaitSecondEot) {
        retry(QByteArray(1, XyModem::nak));
        return;
    }

    const auto previousBlock =
            static_cast<quint8>(
                    expectedDownloadBlock_ - 1U);
    if (packet.number == previousBlock) {
        if (protocol_ == Protocol::Ymodem && packet.number == 0 && expectedDownloadBlock_ == 1 && downloadOffset_ == 0) {
            respond(QByteArray(1, XyModem::ack) + QByteArray(1, XyModem::crcRequest));
        } else {
            respond(QByteArray(1, XyModem::ack));
        }
        return;
    }
    if (packet.number != expectedDownloadBlock_) {
        retry(QByteArray(1, XyModem::nak));
        return;
    }

    if (!writeReceivedData(packet.data)) {
        return;
    }
    ++expectedDownloadBlock_;
    resetRetry();
    state_ = State::ReceiveWaitData;
    respond(QByteArray(1, XyModem::ack));

    const qint64 shownOffset =
            downloadSize_ >= 0
                    ? downloadOffset_
                    : downloadOffset_ + pendingDownloadData_.size();
    emit fileProgress(protocol_,
                      direction_,
                      downloadFileName_,
                      shownOffset,
                      downloadSize_);
}

void XyModemTransfer::handleReceiveEot() {
    if (protocol_ == Protocol::Xmodem && state_ == State::ReceiveWaitData) {
        if (!finishReceivedFile()) {
            return;
        }
        emit outboundData(QByteArray(1, XyModem::ack));
        completeTransfer();
        return;
    }

    if (protocol_ == Protocol::Ymodem && state_ == State::ReceiveWaitData) {
        state_ = State::ReceiveWaitSecondEot;
        respond(QByteArray(1, XyModem::nak));
        return;
    }

    if (protocol_ == Protocol::Ymodem && state_ == State::ReceiveWaitSecondEot) {
        if (!finishReceivedFile()) {
            return;
        }
        state_ = State::ReceiveWaitMetadata;
        respond(QByteArray(1, XyModem::ack) + QByteArray(1, XyModem::crcRequest));
        return;
    }

    if (protocol_ == Protocol::Ymodem && state_ == State::ReceiveWaitMetadata && downloadedFileCount_ > 0) {
        respond(QByteArray(1, XyModem::ack) + QByteArray(1, XyModem::crcRequest));
        return;
    }

    retry(QByteArray(1, XyModem::nak));
}

bool XyModemTransfer::validateUploadPaths(
        const Protocol protocol,
        const QStringList &filePaths,
        QString *errorMessage) {
    if (filePaths.isEmpty() || (protocol == Protocol::Xmodem && filePaths.size() != 1)) {
        *errorMessage =
                protocol == Protocol::Xmodem
                        ? tr("XMODEM sends exactly one file.")
                        : tr("Select at least one file for YMODEM.");
        return false;
    }

    qint64 bytesLeft = 0;
    for (const QString &filePath: filePaths) {
        const QFileInfo fileInfo(filePath);
        if (!fileInfo.isFile() || !fileInfo.isReadable()) {
            *errorMessage = tr("Cannot read file: %1")
                                    .arg(QDir::toNativeSeparators(
                                            filePath));
            return false;
        }
        if (fileInfo.size() > std::numeric_limits<qint64>::max() - bytesLeft) {
            *errorMessage =
                    tr("The selected files are too large.");
            return false;
        }
        bytesLeft += fileInfo.size();
    }

    if (protocol == Protocol::Ymodem) {
        qint64 remainingBytes = bytesLeft;
        for (int index = 0;
             index < filePaths.size();
             ++index) {
            const QFileInfo fileInfo(
                    filePaths.at(index));
            if (fileInfo.fileName().isEmpty() || makeMetadata(
                                                         fileInfo,
                                                         static_cast<int>(
                                                                 filePaths.size() - index),
                                                         remainingBytes)
                                                                 .size() > XyModem::shortBlockSize) {
                *errorMessage = tr(
                                        "The YMODEM file name is too long: %1")
                                        .arg(fileInfo.fileName());
                return false;
            }
            remainingBytes -= fileInfo.size();
        }
    }
    return true;
}

QByteArray XyModemTransfer::uploadMetadata(
        const int index) const {
    qint64 bytesLeft = 0;
    for (int remainingIndex = index;
         remainingIndex < uploadPaths_.size();
         ++remainingIndex) {
        bytesLeft += QFileInfo(
                             uploadPaths_.at(
                                     remainingIndex))
                             .size();
    }
    return makeMetadata(
            QFileInfo(uploadPaths_.at(index)),
            static_cast<int>(
                    uploadPaths_.size() - index),
            bytesLeft);
}

void XyModemTransfer::offerCurrentUpload() {
    if (protocol_ == Protocol::Ymodem && uploadIndex_ >= uploadPaths_.size()) {
        transmit(XyModem::encodePacket(
                         0,
                         {},
                         XyModem::shortBlockSize,
                         XyModem::CheckMode::Crc16,
                         '\0'),
                 State::SendWaitBatchEndAck);
        return;
    }

    uploadFile_.setFileName(
            uploadPaths_.at(uploadIndex_));
    if (!uploadFile_.open(QIODevice::ReadOnly)) {
        failTransfer(
                formatError(tr("Cannot open upload file"),
                            uploadFile_),
                true);
        return;
    }

    const QFileInfo fileInfo(uploadFile_);
    uploadFileName_ = fileInfo.fileName();
    uploadSize_ = fileInfo.size();
    uploadOffset_ = 0;
    uploadPacketBytes_ = 0;
    uploadBlockNumber_ = 1;
    emit fileStarted(protocol_,
                     direction_,
                     uploadFileName_,
                     uploadSize_,
                     uploadIndex_ + 1,
                     static_cast<int>(
                             uploadPaths_.size()));

    if (protocol_ == Protocol::Ymodem) {
        transmit(XyModem::encodePacket(
                         0,
                         uploadMetadata(uploadIndex_),
                         XyModem::shortBlockSize,
                         XyModem::CheckMode::Crc16,
                         '\0'),
                 State::SendWaitMetadataAck);
        return;
    }
    sendNextUploadBlock();
}

void XyModemTransfer::sendNextUploadBlock() {
    if (uploadOffset_ >= uploadSize_) {
        transmit(QByteArray(1, XyModem::eot),
                 State::SendWaitFirstEotResponse);
        return;
    }

    const int blockSize =
            protocol_ == Protocol::Xmodem
                    ? XyModem::shortBlockSize
                    : XyModem::longBlockSize;
    const qint64 bytesToRead =
            std::min<qint64>(
                    blockSize,
                    uploadSize_ - uploadOffset_);
    const QByteArray data = uploadFile_.read(bytesToRead);
    if (data.size() != bytesToRead) {
        failTransfer(
                formatError(tr("Cannot read upload file"),
                            uploadFile_),
                true);
        return;
    }

    uploadPacketBytes_ = data.size();
    transmit(XyModem::encodePacket(
                     uploadBlockNumber_,
                     data,
                     blockSize,
                     checkMode_),
             State::SendWaitDataAck);
}

void XyModemTransfer::finishUploadedFile() {
    emit fileProgress(protocol_,
                      direction_,
                      uploadFileName_,
                      uploadSize_,
                      uploadSize_);
    emit fileFinished(
            protocol_,
            direction_,
            uploadPaths_.at(uploadIndex_));
    uploadFile_.close();
    uploadFile_.setFileName({});
    uploadFileName_.clear();
    uploadSize_ = 0;
    uploadOffset_ = 0;
    uploadPacketBytes_ = 0;
    ++uploadedFileCount_;
    ++uploadIndex_;
}

void XyModemTransfer::handleDownloadMetadata(
        const QByteArray &data) {
    const qsizetype nameEnd = data.indexOf('\0');
    if (nameEnd <= 0) {
        retry(QByteArray(1, XyModem::nak));
        return;
    }

    const QByteArray remoteName = data.left(nameEnd);
    const QByteArray properties =
            data.mid(nameEnd + 1)
                    .split('\0')
                    .value(0);
    const QList<QByteArray> fields =
            properties.split(' ');

    bool sizeOk = false;
    const qint64 size =
            fields.value(0).toLongLong(&sizeOk, 10);
    downloadSize_ = sizeOk && size >= 0 ? size : -1;

    bool modificationTimeOk = false;
    const qint64 modificationTime =
            fields.value(1).toLongLong(
                    &modificationTimeOk, 8);
    downloadModificationTime_ =
            modificationTimeOk
                    ? modificationTime
                    : -1;

    bool fileCountOk = false;
    const int filesLeft =
            fields.value(4).toInt(&fileCountOk, 10);
    if (fileCountOk && filesLeft > 0) {
        expectedDownloadFileCount_ =
                std::max(
                        expectedDownloadFileCount_,
                        downloadedFileCount_ + filesLeft);
    }

    downloadFileName_ =
            safeDownloadName(remoteName);
    if (downloadFileName_.isEmpty()) {
        failTransfer(
                tr("The remote side sent an invalid YMODEM file name."),
                true);
        return;
    }
    downloadFilePath_ =
            uniqueDownloadPath(downloadFileName_);
    downloadFile_ =
            std::make_unique<QSaveFile>(
                    downloadFilePath_);
    if (!downloadFile_->open(QIODevice::WriteOnly)) {
        failTransfer(
                formatError(tr("Cannot create download file"),
                            *downloadFile_),
                true);
        return;
    }

    downloadOffset_ = 0;
    pendingDownloadData_.clear();
    expectedDownloadBlock_ = 1;
    state_ = State::ReceiveWaitData;
    emit fileStarted(
            protocol_,
            direction_,
            downloadFileName_,
            downloadSize_,
            downloadedFileCount_ + 1,
            expectedDownloadFileCount_);
    respond(QByteArray(1, XyModem::ack) + QByteArray(1, XyModem::crcRequest));
}

bool XyModemTransfer::writeReceivedData(
        const QByteArray &data) {
    if (downloadFile_ == nullptr) {
        failTransfer(
                tr("No X/YMODEM download file is open."),
                true);
        return false;
    }

    if (downloadSize_ >= 0) {
        const qint64 remaining =
                std::max<qint64>(
                        downloadSize_ - downloadOffset_,
                        0);
        const QByteArray fileData =
                data.left(
                        static_cast<qsizetype>(
                                std::min<qint64>(
                                        remaining,
                                        data.size())));
        if (!fileData.isEmpty() && downloadFile_->write(fileData) != fileData.size()) {
            failTransfer(
                    formatError(
                            tr("Cannot write download file"),
                            *downloadFile_),
                    true);
            return false;
        }
        downloadOffset_ += fileData.size();
        return true;
    }

    if (!pendingDownloadData_.isEmpty()) {
        if (downloadFile_->write(
                    pendingDownloadData_) != pendingDownloadData_.size()) {
            failTransfer(
                    formatError(
                            tr("Cannot write download file"),
                            *downloadFile_),
                    true);
            return false;
        }
        downloadOffset_ +=
                pendingDownloadData_.size();
    }
    pendingDownloadData_ = data;
    return true;
}

bool XyModemTransfer::finishReceivedFile() {
    if (downloadFile_ == nullptr) {
        failTransfer(
                tr("No X/YMODEM download file is open."),
                true);
        return false;
    }

    if (downloadSize_ < 0 && !pendingDownloadData_.isEmpty()) {
        while (pendingDownloadData_.endsWith(
                XyModem::padding)) {
            pendingDownloadData_.chop(1);
        }
        if (!pendingDownloadData_.isEmpty() && downloadFile_->write(
                                                       pendingDownloadData_) != pendingDownloadData_.size()) {
            failTransfer(
                    formatError(
                            tr("Cannot write download file"),
                            *downloadFile_),
                    true);
            return false;
        }
        downloadOffset_ +=
                pendingDownloadData_.size();
        pendingDownloadData_.clear();
    }

    if (downloadSize_ >= 0 && downloadOffset_ != downloadSize_) {
        failTransfer(
                tr("The YMODEM file ended before its declared size."),
                true);
        return false;
    }
    if (!downloadFile_->commit()) {
        failTransfer(
                formatError(tr("Cannot save downloaded file"),
                            *downloadFile_),
                true);
        return false;
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

    emit fileProgress(protocol_,
                      direction_,
                      downloadFileName_,
                      downloadOffset_,
                      downloadSize_);
    emit fileFinished(protocol_,
                      direction_,
                      downloadFilePath_);
    ++downloadedFileCount_;
    downloadFile_.reset();
    downloadFileName_.clear();
    downloadFilePath_.clear();
    downloadSize_ = -1;
    downloadOffset_ = 0;
    downloadModificationTime_ = -1;
    pendingDownloadData_.clear();
    expectedDownloadBlock_ = 1;
    return true;
}

QString XyModemTransfer::safeDownloadName(
        const QByteArray &remoteName) {
    QString normalizedName =
            QString::fromUtf8(remoteName);
    normalizedName.replace('\\', '/');
    QString fileName =
            normalizedName.section('/', -1, -1)
                    .trimmed();
    if (fileName.isEmpty() || fileName == QStringLiteral(".") || fileName == QStringLiteral("..")) {
        return {};
    }
    return fileName;
}

QString XyModemTransfer::uniqueDownloadPath(
        const QString &fileName) const {
    QDir directory(downloadDestination_);
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

void XyModemTransfer::closeDownloadFile() {
    if (downloadFile_ != nullptr) {
        downloadFile_->cancelWriting();
        downloadFile_.reset();
    }
    downloadFileName_.clear();
    downloadFilePath_.clear();
    downloadSize_ = -1;
    downloadOffset_ = 0;
    downloadModificationTime_ = -1;
    pendingDownloadData_.clear();
}

void XyModemTransfer::transmit(
        const QByteArray &data,
        const State state) {
    state_ = state;
    retryPacket_ = data;
    retryCount_ = 0;
    timeout_.start();
    emit outboundData(data);
}

void XyModemTransfer::respond(
        const QByteArray &data) {
    retryPacket_ = data;
    retryCount_ = 0;
    protocolErrorCount_ = 0;
    timeout_.start();
    emit outboundData(data);
}

bool XyModemTransfer::retry(
        const QByteArray &data) {
    ++retryCount_;
    if (retryCount_ > maximumRetries) {
        failTransfer(
                tr("The X/YMODEM peer did not respond."),
                true);
        return false;
    }
    retryPacket_ = data;
    timeout_.start();
    emit outboundData(data);
    return true;
}

void XyModemTransfer::resetRetry() {
    timeout_.stop();
    retryPacket_.clear();
    retryCount_ = 0;
    protocolErrorCount_ = 0;
}

void XyModemTransfer::completeTransfer() {
    const Protocol completedProtocol = protocol_;
    const Direction completedDirection = direction_;
    const int fileCount =
            completedDirection == Direction::Upload
                    ? uploadedFileCount_
                    : downloadedFileCount_;
    resetTransfer();
    emit transferFinished(completedProtocol,
                          completedDirection,
                          fileCount);
}

void XyModemTransfer::failTransfer(
        const QString &message,
        const bool notifyPeer) {
    const Protocol failedProtocol = protocol_;
    const Direction failedDirection = direction_;
    if (notifyPeer) {
        emit outboundData(XyModem::cancelSequence());
    }
    resetTransfer();
    emit transferFailed(failedProtocol,
                        failedDirection,
                        message);
}

void XyModemTransfer::handleRemoteCancel() {
    const Protocol failedProtocol = protocol_;
    const Direction failedDirection = direction_;
    resetTransfer();
    emit transferFailed(
            failedProtocol,
            failedDirection,
            tr("The remote side canceled the X/YMODEM transfer."));
}

void XyModemTransfer::resetTransfer() {
    timeout_.stop();
    state_ = State::Idle;
    retryPacket_.clear();
    retryCount_ = 0;
    protocolErrorCount_ = 0;

    if (uploadFile_.isOpen()) {
        uploadFile_.close();
    }
    uploadFile_.setFileName({});
    uploadPaths_.clear();
    uploadIndex_ = 0;
    uploadFileName_.clear();
    uploadSize_ = 0;
    uploadOffset_ = 0;
    uploadPacketBytes_ = 0;
    uploadBlockNumber_ = 1;
    uploadedFileCount_ = 0;

    closeDownloadFile();
    downloadDestination_.clear();
    expectedDownloadBlock_ = 1;
    downloadedFileCount_ = 0;
    expectedDownloadFileCount_ = 0;
    initialCrcRequestCount_ = 0;
}

void XyModemTransfer::onTimeout() {
    if (!isActive()) {
        return;
    }

    inputBuffer_.clear();
    switch (state_) {
        case State::SendWaitStart:
        case State::SendWaitNextFileRequest:
            ++retryCount_;
            if (retryCount_ > maximumRetries) {
                failTransfer(
                        tr("The X/YMODEM peer did not start the transfer."),
                        true);
            } else {
                timeout_.start();
            }
            break;
        case State::SendWaitMetadataAck:
        case State::SendWaitMetadataRequest:
        case State::SendWaitDataAck:
        case State::SendWaitFirstEotResponse:
        case State::SendWaitFinalEotAck:
        case State::SendWaitBatchEndAck:
            retry(retryPacket_);
            break;
        case State::ReceiveWaitMetadata:
            retry(QByteArray(1, XyModem::crcRequest));
            break;
        case State::ReceiveWaitData:
            if (protocol_ == Protocol::Xmodem && expectedDownloadBlock_ == 1 && downloadOffset_ == 0 && pendingDownloadData_.isEmpty() && checkMode_ == XyModem::CheckMode::Crc16) {
                if (initialCrcRequestCount_ < crcRequestsBeforeChecksumFallback) {
                    ++initialCrcRequestCount_;
                    retry(QByteArray(
                            1, XyModem::crcRequest));
                } else {
                    checkMode_ =
                            XyModem::CheckMode::Checksum;
                    retryCount_ = 0;
                    retry(QByteArray(1, XyModem::nak));
                }
            } else if (protocol_ == Protocol::Ymodem && expectedDownloadBlock_ == 1 && downloadOffset_ == 0) {
                retry(QByteArray(
                        1, XyModem::crcRequest));
            } else {
                retry(QByteArray(1, XyModem::nak));
            }
            break;
        case State::ReceiveWaitSecondEot:
            retry(QByteArray(1, XyModem::nak));
            break;
        case State::Idle:
            break;
    }
}
