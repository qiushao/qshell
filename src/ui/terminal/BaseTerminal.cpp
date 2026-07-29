#include "BaseTerminal.h"

#include "core/ConfigManager.h"
#include "ptyqt.h"
#include <QDebug>
#include <QDir>
#include <QProcess>
#include <QContextMenuEvent>
#include <QMenu>
#include <QFileDialog>
#include <QMessageBox>
#include <QDateTime>
#include <QColorDialog>
#include <QProgressDialog>
#include <QRandomGenerator>
#include <QTextStream>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace {

constexpr int pendingSendTimeoutMilliseconds = 5000;
constexpr int pendingReceiveDelayMilliseconds = 300;
constexpr qsizetype maximumPendingOutputSize = 4096;
constexpr qint64 zmodemRateUpdateIntervalMilliseconds = 500;
constexpr double bytesPerKilobyte = 1024.0;
constexpr double bytesPerMegabyte =
        bytesPerKilobyte * bytesPerKilobyte;
constexpr double bytesPerGigabyte =
        bytesPerMegabyte * bytesPerKilobyte;

QString formatTransferRate(
        const qint64 transferred,
        const qint64 elapsedMilliseconds) {
    if (transferred <= 0 || elapsedMilliseconds <= 0) {
        return QStringLiteral("0 B/s");
    }

    const double bytesPerSecond =
            static_cast<double>(transferred) * 1000.0 / static_cast<double>(elapsedMilliseconds);
    if (bytesPerSecond >= bytesPerGigabyte) {
        return QStringLiteral("%1 GB/s")
                .arg(bytesPerSecond / bytesPerGigabyte,
                     0,
                     'f',
                     1);
    }
    if (bytesPerSecond >= bytesPerMegabyte) {
        return QStringLiteral("%1 MB/s")
                .arg(bytesPerSecond / bytesPerMegabyte,
                     0,
                     'f',
                     1);
    }
    if (bytesPerSecond >= bytesPerKilobyte) {
        return QStringLiteral("%1 KB/s")
                .arg(bytesPerSecond / bytesPerKilobyte,
                     0,
                     'f',
                     1);
    }
    return QStringLiteral("%1 B/s")
            .arg(bytesPerSecond, 0, 'f', 0);
}

QString formatRemainingTime(
        const qint64 remaining,
        const qint64 transferred,
        const qint64 elapsedMilliseconds) {
    if (remaining <= 0) {
        return QStringLiteral("00:00:00");
    }
    if (transferred <= 0 || elapsedMilliseconds <= 0) {
        return QStringLiteral("--");
    }

    const double bytesPerSecond =
            static_cast<double>(transferred) * 1000.0 / static_cast<double>(elapsedMilliseconds);
    const qint64 remainingSeconds =
            std::max<qint64>(
                    1,
                    static_cast<qint64>(
                            std::ceil(static_cast<double>(remaining) / bytesPerSecond)));
    const qint64 hours = remainingSeconds / 3600;
    const qint64 minutes = (remainingSeconds % 3600) / 60;
    const qint64 seconds = remainingSeconds % 60;
    return QStringLiteral("%1:%2:%3")
            .arg(hours, 2, 10, QLatin1Char('0'))
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
}

QString xyModemName(const XyModemTransfer::Protocol protocol) {
    return protocol == XyModemTransfer::Protocol::Xmodem
                   ? QStringLiteral("XMODEM")
                   : QStringLiteral("YMODEM");
}

XyModemTransfer::Protocol xyModemProtocol(
        const XyModemCommand command) {
    return command == XyModemCommand::SendXmodem || command == XyModemCommand::ReceiveXmodem
                   ? XyModemTransfer::Protocol::Xmodem
                   : XyModemTransfer::Protocol::Ymodem;
}

}// namespace

BaseTerminal::BaseTerminal(QWidget *parent) : QTermWidget(parent, parent) {
    connect_ = false;
    logging_ = false;
    logFile_ = nullptr;

    auto globalSettings = ConfigManager::instance()->globalSettings();

    font_ = new QFont();
    font_->setFamily(globalSettings.fontFamily);
    font_->setPointSize(globalSettings.fontSize);
    setTerminalFont(*font_);
    setHistorySize(128000);
    setTerminalSizeHint(false);
    setUrlFilterEnabled(false);
    setColorScheme(globalSettings.colorScheme);
    setScrollBarPosition(ScrollBarRight);
    setConfirmMultilinePaste(false);

    if (globalSettings.copyOnSelect) {
        QObject::connect(this, &QTermWidget::copyAvailable, this, &BaseTerminal::onCopyAvailable);
    }

    QObject::connect(this, &QTermWidget::onNewLine, this, &BaseTerminal::onDisplayOutput);

    xyModemTransfer_ = new XyModemTransfer(this);
    zmodemTransfer_ = new ZmodemTransfer(this);
    pendingXyModemTimer_.setSingleShot(true);
    QObject::connect(
            &pendingXyModemTimer_,
            &QTimer::timeout,
            this,
            [this]() {
                if (isXyModemSendCommand(
                            pendingXyModemCommand_)) {
                    clearPendingXyModemCommand();
                    return;
                }
                startPendingXyModemTransfer();
            });
    QObject::connect(this, &QTermWidget::sendData, this,
                     [this](const char *data, int size) {
                         if (xyModemTransfer_->isActive()) {
                             if (size == 1 && data[0] == 0x03) {
                                 xyModemTransfer_->cancel();
                             }
                             return;
                         }
                         if (zmodemTransfer_->isActive()) {
                             if (size == 1 && data[0] == 0x03) {
                                 zmodemTransfer_->cancel();
                             }
                             return;
                         }
                         if (pendingXyModemCommand_ != XyModemCommand::None && size == 1 && data[0] == 0x03) {
                             clearPendingXyModemCommand();
                         }
                         const QByteArray outboundData(data, size);
                         writeToBackend(outboundData);
                         const XyModemCommand command =
                                 xyModemCommandDetector_.consume(
                                         outboundData);
                         if (command == XyModemCommand::None) {
                             return;
                         }
                         beginPendingXyModemCommand(command);
                     });
    QObject::connect(xyModemTransfer_,
                     &XyModemTransfer::outboundData,
                     this,
                     [this](const QByteArray &data) {
                         writeToBackend(data);
                     });
    QObject::connect(xyModemTransfer_,
                     &XyModemTransfer::fileStarted,
                     this,
                     &BaseTerminal::onXyModemFileStarted);
    QObject::connect(xyModemTransfer_,
                     &XyModemTransfer::fileProgress,
                     this,
                     &BaseTerminal::onXyModemFileProgress);
    QObject::connect(
            xyModemTransfer_,
            &XyModemTransfer::transferFinished,
            this,
            [this](XyModemTransfer::Protocol protocol,
                   XyModemTransfer::Direction direction,
                   int fileCount) {
                closeXyModemProgress();
                QTimer::singleShot(
                        0,
                        this,
                        [this, protocol, direction, fileCount]() {
                            const QString action =
                                    direction == XyModemTransfer::Direction::Download
                                            ? tr("下载")
                                            : tr("上传");
                            const QString protocolName =
                                    xyModemName(protocol);
                            QMessageBox::information(
                                    this,
                                    tr("%1 文件传输")
                                            .arg(protocolName),
                                    tr("%1 %2完成，共传输 %3 个文件。")
                                            .arg(protocolName, action)
                                            .arg(fileCount));
                        });
            });
    QObject::connect(
            xyModemTransfer_,
            &XyModemTransfer::transferCanceled,
            this,
            [this](XyModemTransfer::Protocol,
                   XyModemTransfer::Direction) {
                closeXyModemProgress();
            });
    QObject::connect(
            xyModemTransfer_,
            &XyModemTransfer::transferFailed,
            this,
            [this](XyModemTransfer::Protocol protocol,
                   XyModemTransfer::Direction,
                   const QString &message) {
                closeXyModemProgress();
                QTimer::singleShot(
                        0,
                        this,
                        [this, protocol, message]() {
                            QMessageBox::warning(
                                    this,
                                    tr("%1 文件传输失败")
                                            .arg(xyModemName(protocol)),
                                    message);
                        });
            });
    QObject::connect(zmodemTransfer_, &ZmodemTransfer::outboundData,
                     this, [this](const QByteArray &data) {
                         writeToBackend(data);
                     });
    QObject::connect(zmodemTransfer_,
                     &ZmodemTransfer::terminalDataReady,
                     this,
                     [this](const QByteArray &data) {
                         recvData(data.constData(),
                                  static_cast<int>(data.size()));
                     });
    QObject::connect(zmodemTransfer_, &ZmodemTransfer::detected,
                     this, [this](ZmodemTransfer::Direction direction) {
                         QTimer::singleShot(0, this, [this, direction]() {
                             onZmodemDetected(direction);
                         });
                     });
    QObject::connect(zmodemTransfer_, &ZmodemTransfer::fileStarted,
                     this, &BaseTerminal::onZmodemFileStarted);
    QObject::connect(zmodemTransfer_, &ZmodemTransfer::fileProgress,
                     this, &BaseTerminal::onZmodemFileProgress);
    QObject::connect(zmodemTransfer_, &ZmodemTransfer::transferFinished,
                     this, [this](ZmodemTransfer::Direction direction,
                                  int fileCount) {
                         closeZmodemProgress();
                         QTimer::singleShot(0, this, [this, direction, fileCount]() {
                             const QString action =
                                     direction == ZmodemTransfer::Direction::Download
                                             ? tr("下载")
                                             : tr("上传");
                             QMessageBox::information(
                                     this,
                                     tr("ZMODEM 文件传输"),
                                     tr("ZMODEM %1完成，共传输 %2 个文件。")
                                             .arg(action)
                                             .arg(fileCount));
                         });
                     });
    QObject::connect(zmodemTransfer_, &ZmodemTransfer::transferCanceled,
                     this, [this](ZmodemTransfer::Direction) {
                         closeZmodemProgress();
                     });
    QObject::connect(zmodemTransfer_, &ZmodemTransfer::transferFailed,
                     this, [this](ZmodemTransfer::Direction,
                                  const QString &message) {
                         closeZmodemProgress();
                         QTimer::singleShot(0, this, [this, message]() {
                             QMessageBox::warning(this,
                                                  tr("ZMODEM 文件传输失败"),
                                                  message);
                         });
                     });

    // 启用右键菜单
    setContextMenuPolicy(Qt::DefaultContextMenu);
}

BaseTerminal::~BaseTerminal() {
    // 确保关闭日志文件
    stopLogging();
    closeXyModemProgress();
    closeZmodemProgress();

    delete font_;
    font_ = nullptr;
}

void BaseTerminal::startLocalShell() {
    QString shellPath;
    QStringList args;
    QStringList envs = QProcessEnvironment::systemEnvironment().toStringList();
    envs.append("TERM=xterm-256color");

#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
    shellPath = qEnvironmentVariable("SHELL");
#elif defined(Q_OS_WIN)
    shellPath = "C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
#endif

    // 明确指定 ConPty
    localShell_ = PtyQt::createPtyProcess();
    if (!localShell_) {
        qWarning() << "Failed to create ConPty process!";
        return;
    }

    // 连接大小变化
    QObject::connect(this, &QTermWidget::termSizeChange, this, [this](int lines, int columns) {
        if (localShell_) {
            localShell_->resize(static_cast<qint16>(columns), static_cast<qint16>(lines));
        }
    });

    // 启动进程
    bool ret = localShell_->startProcess(
        shellPath,
        args,
        QDir::homePath(),
        envs,
        static_cast<qint16>(screenColumnsCount()),
        static_cast<qint16>(screenLinesCount())
    );

    if (!ret) {
        qWarning() << "startProcess failed:" << localShell_->lastError();
        return;
    }

    // 连接 notifier（如果可用）
    if (QIODevice* notifier = localShell_->notifier()) {
        QObject::connect(notifier, &QIODevice::readyRead, this, [this]() {
            QByteArray data = localShell_->readAll();
            if (!data.isEmpty()) {
                receiveBackendData(data);
            }
        });
    } else {
        qDebug() << "localShell_->notifier() is nullptr";
    }

    connect_ = true;

    auto syncPtySize = [this]() {
        if (localShell_) {
            localShell_->resize(static_cast<qint16>(screenColumnsCount()),
                                static_cast<qint16>(screenLinesCount()));
        }
    };
    QTimer::singleShot(0, this, syncPtySize);
    QTimer::singleShot(100, this, syncPtySize);
}

bool BaseTerminal::isConnect() const {
    return connect_;
}

bool BaseTerminal::isLogging() const {
    return logging_;
}

QString BaseTerminal::logFilePath() const {
    return logFilePath_;
}

QString BaseTerminal::getSessionName() const {
    return sessionData_.name;
}

void BaseTerminal::onDisplayOutput(const QString &line) {
    // 如果正在记录日志，写入数据
    if (logging_ && logFile_ && logFile_->isOpen()) {
        writeToLog(line);
    }
}

void BaseTerminal::onCopyAvailable(bool copyAvailable) {
    if (copyAvailable) {
        copyClipboard();
    }
}

void BaseTerminal::receiveBackendData(const QByteArray &data) {
    if (xyModemTransfer_->isActive()) {
        const QByteArray terminalData =
                xyModemTransfer_->consume(data);
        if (!terminalData.isEmpty()) {
            recvData(terminalData.constData(),
                     static_cast<int>(terminalData.size()));
        }
        return;
    }
    if (pendingXyModemCommand_ != XyModemCommand::None) {
        processPendingXyModemData(data);
        return;
    }
    displayBackendData(data);
}

void BaseTerminal::displayBackendData(
        const QByteArray &data) {
    zmodemTransfer_->enqueueData(data);
}

bool BaseTerminal::prepareZmodemUpload(
        const QStringList &filePaths) {
    if (!isConnect() || zmodemTransfer_->isActive()
        || filePaths.isEmpty()) {
        return false;
    }

    QStringList absolutePaths;
    absolutePaths.reserve(filePaths.size());
    for (const QString &filePath: filePaths) {
        const QFileInfo fileInfo(filePath);
        if (!fileInfo.isFile() || !fileInfo.isReadable()) {
            return false;
        }
        absolutePaths.append(fileInfo.absoluteFilePath());
    }

    pendingZmodemUploadPaths_ = absolutePaths;
    pendingZmodemDownloadDirectory_.clear();
    return true;
}

bool BaseTerminal::prepareZmodemDownload(
        const QString &directoryPath) {
    if (!isConnect() || zmodemTransfer_->isActive()) {
        return false;
    }

    const QFileInfo directoryInfo(directoryPath);
    if (!directoryInfo.isDir() || !directoryInfo.isWritable()) {
        return false;
    }

    pendingZmodemDownloadDirectory_ =
            directoryInfo.absoluteFilePath();
    pendingZmodemUploadPaths_.clear();
    return true;
}

void BaseTerminal::beginPendingXyModemCommand(
        const XyModemCommand command) {
    clearPendingXyModemCommand();
    if (!isConnect()) {
        return;
    }

    pendingXyModemCommand_ = command;
    pendingXyModemTimer_.start(
            isXyModemSendCommand(command)
                    ? pendingSendTimeoutMilliseconds
                    : pendingReceiveDelayMilliseconds);
}

void BaseTerminal::processPendingXyModemData(
        const QByteArray &data) {
    if (isXyModemSendCommand(
                pendingXyModemCommand_) &&
        pendingXyModemDialogScheduled_) {
        if (data.contains(XyModem::can)) {
            clearPendingXyModemCommand();
        }
        return;
    }

    pendingXyModemOutput_.append(data);
    if (pendingXyModemOutput_.size() > maximumPendingOutputSize) {
        pendingXyModemOutput_.remove(
                0,
                pendingXyModemOutput_.size() - maximumPendingOutputSize);
    }
    if (containsXyModemCommandFailure(
                pendingXyModemOutput_)) {
        clearPendingXyModemCommand();
        displayBackendData(data);
        return;
    }

    if (!isXyModemSendCommand(
                pendingXyModemCommand_)) {
        displayBackendData(data);
        return;
    }
    if (data.contains(XyModem::can)) {
        clearPendingXyModemCommand();
        return;
    }

    const qsizetype handshakeIndex =
            findXyModemReceiverHandshake(
                    pendingXyModemCommand_, data);
    if (handshakeIndex < 0) {
        displayBackendData(data);
        return;
    }

    if (handshakeIndex > 0) {
        displayBackendData(data.left(handshakeIndex));
    }
    pendingXyModemProtocolData_ =
            QByteArray(1, data.at(handshakeIndex));
    pendingXyModemTimer_.stop();
    pendingXyModemDialogScheduled_ = true;
    QTimer::singleShot(
            0,
            this,
            &BaseTerminal::startPendingXyModemTransfer);
}

void BaseTerminal::clearPendingXyModemCommand() {
    pendingXyModemTimer_.stop();
    if (pendingXyModemDialogScheduled_ && xyModemFileDialog_ != nullptr) {
        xyModemFileDialog_->reject();
    }
    pendingXyModemCommand_ = XyModemCommand::None;
    pendingXyModemOutput_.clear();
    pendingXyModemProtocolData_.clear();
    pendingXyModemDialogScheduled_ = false;
}

void BaseTerminal::onZmodemDetected(
        ZmodemTransfer::Direction direction) {
    if (!zmodemTransfer_->isActive()
        || zmodemTransfer_->direction() != direction) {
        return;
    }

    const QString initialDirectory =
            zmodemDirectory_.isEmpty()
                    ? QDir::homePath()
                    : zmodemDirectory_;
    if (direction == ZmodemTransfer::Direction::Download) {
        if (!pendingZmodemDownloadDirectory_.isEmpty()) {
            const QString directory =
                    pendingZmodemDownloadDirectory_;
            pendingZmodemDownloadDirectory_.clear();
            zmodemDirectory_ = directory;
            zmodemTransfer_->acceptDownload(directory);
            return;
        }

        const QString directory = QFileDialog::getExistingDirectory(
                this,
                tr("选择 ZMODEM 下载目录"),
                initialDirectory,
                QFileDialog::ShowDirsOnly);
        if (directory.isEmpty()) {
            zmodemTransfer_->reject();
            return;
        }
        zmodemDirectory_ = directory;
        zmodemTransfer_->acceptDownload(directory);
        return;
    }

    if (!pendingZmodemUploadPaths_.isEmpty()) {
        const QStringList files = pendingZmodemUploadPaths_;
        pendingZmodemUploadPaths_.clear();
        zmodemDirectory_ =
                QFileInfo(files.constFirst()).absolutePath();
        zmodemTransfer_->acceptUpload(files);
        return;
    }

    const QStringList files = QFileDialog::getOpenFileNames(
            this,
            tr("选择要通过 ZMODEM 上传的文件"),
            initialDirectory,
            tr("所有文件 (*)"));
    if (files.isEmpty()) {
        zmodemTransfer_->reject();
        return;
    }
    zmodemDirectory_ = QFileInfo(files.constFirst()).absolutePath();
    zmodemTransfer_->acceptUpload(files);
}

void BaseTerminal::onZmodemFileStarted(
        ZmodemTransfer::Direction direction,
        const QString &fileName,
        qint64 size,
        int fileNumber,
        int fileCount) {
    closeZmodemProgress();

    const QString action =
            direction == ZmodemTransfer::Direction::Download
                    ? tr("正在下载")
                    : tr("正在上传");
    zmodemProgressLabel_ =
            tr("%1 %2（%3/%4）")
                    .arg(action, fileName)
                    .arg(fileNumber)
                    .arg(fileCount);
    zmodemRateTransferred_ = 0;
    zmodemRateTimer_.start();
    zmodemProgress_ = new QProgressDialog(this);
    zmodemProgress_->setWindowTitle(tr("ZMODEM 文件传输"));
    zmodemProgress_->setLabelText(
            tr("%1\n传输速率：%2\n预计剩余时间：%3")
                    .arg(zmodemProgressLabel_,
                         QStringLiteral("--"),
                         QStringLiteral("--")));
    zmodemProgress_->setCancelButtonText(tr("取消"));
    zmodemProgress_->setRange(0, size <= 0 ? 0 : 1000);
    zmodemProgress_->setValue(0);
    zmodemProgress_->setMinimumDuration(0);
    zmodemProgress_->setAutoClose(false);
    zmodemProgress_->setAutoReset(false);
    zmodemProgress_->setWindowModality(Qt::NonModal);
    QObject::connect(zmodemProgress_, &QProgressDialog::canceled,
                     zmodemTransfer_, &ZmodemTransfer::cancel);
    zmodemProgress_->show();
}

void BaseTerminal::onZmodemFileProgress(
        ZmodemTransfer::Direction,
        const QString &,
        qint64 transferred,
        qint64 size) {
    if (zmodemProgress_ == nullptr) {
        return;
    }

    const qint64 elapsedMilliseconds =
            zmodemRateTimer_.elapsed();
    const bool fileComplete =
            size > 0 && transferred >= size;
    const bool hasNewCompletedBytes =
            fileComplete && transferred > zmodemRateTransferred_;
    if (elapsedMilliseconds < zmodemRateUpdateIntervalMilliseconds && !hasNewCompletedBytes) {
        return;
    }
    if (elapsedMilliseconds > 0) {
        const qint64 transferredSinceUpdate =
                std::max<qint64>(
                        0,
                        transferred - zmodemRateTransferred_);
        const qint64 remaining =
                size > 0
                        ? size - std::clamp<qint64>(
                                         transferred,
                                         0,
                                         size)
                        : -1;
        zmodemProgress_->setLabelText(
                tr("%1\n传输速率：%2\n预计剩余时间：%3")
                        .arg(zmodemProgressLabel_,
                             formatTransferRate(
                                     transferredSinceUpdate,
                                     elapsedMilliseconds),
                             formatRemainingTime(
                                     remaining,
                                     transferredSinceUpdate,
                                     elapsedMilliseconds)));
        zmodemRateTransferred_ = transferred;
        zmodemRateTimer_.restart();
    }

    if (size <= 0) {
        zmodemProgress_->setRange(0, 0);
        return;
    }
    const qint64 boundedTransferred =
            std::clamp<qint64>(transferred, 0, size);
    const int progress =
            static_cast<int>((boundedTransferred * 1000) / size);
    zmodemProgress_->setRange(0, 1000);
    zmodemProgress_->setValue(progress);
}

void BaseTerminal::closeZmodemProgress() {
    if (zmodemProgress_ == nullptr) {
        return;
    }
    QObject::disconnect(zmodemProgress_, nullptr,
                        zmodemTransfer_, nullptr);
    zmodemProgress_->close();
    zmodemProgress_->deleteLater();
    zmodemProgress_ = nullptr;
    zmodemProgressLabel_.clear();
    zmodemRateTimer_.invalidate();
    zmodemRateTransferred_ = 0;
}

void BaseTerminal::startXyModemSend(
        const XyModemTransfer::Protocol protocol) {
    if (!isConnect() || pendingXyModemCommand_ != XyModemCommand::None || xyModemTransfer_->isActive() || zmodemTransfer_->isActive()) {
        return;
    }

    const QStringList files =
            selectXyModemSendFiles(protocol);
    if (files.isEmpty()) {
        return;
    }

    xyModemDirectory_ =
            QFileInfo(files.constFirst()).absolutePath();
    xyModemTransfer_->send(protocol, files);
}

QStringList BaseTerminal::selectXyModemSendFiles(
        const XyModemTransfer::Protocol protocol) {
    const QString initialDirectory =
            xyModemDirectory_.isEmpty()
                    ? QDir::homePath()
                    : xyModemDirectory_;
    QFileDialog dialog(
            this,
            protocol == XyModemTransfer::Protocol::Xmodem
                    ? tr("选择要通过 XMODEM 发送的文件")
                    : tr("选择要通过 YMODEM 发送的文件"),
            initialDirectory,
            tr("所有文件 (*)"));
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    if (protocol == XyModemTransfer::Protocol::Xmodem) {
        dialog.setFileMode(QFileDialog::ExistingFile);
    } else {
        dialog.setFileMode(QFileDialog::ExistingFiles);
    }
    if (!executeXyModemFileDialog(&dialog)) {
        return {};
    }
    return dialog.selectedFiles();
}

void BaseTerminal::startXyModemReceive(
        const XyModemTransfer::Protocol protocol) {
    if (!isConnect() || pendingXyModemCommand_ != XyModemCommand::None || xyModemTransfer_->isActive() || zmodemTransfer_->isActive()) {
        return;
    }

    const QString destination =
            selectXyModemReceiveDestination(protocol);
    if (destination.isEmpty()) {
        return;
    }

    xyModemDirectory_ =
            protocol == XyModemTransfer::Protocol::Xmodem
                    ? QFileInfo(destination).absolutePath()
                    : destination;
    xyModemTransfer_->receive(protocol, destination);
}

QString BaseTerminal::selectXyModemReceiveDestination(
        const XyModemTransfer::Protocol protocol) {
    const QString initialDirectory =
            xyModemDirectory_.isEmpty()
                    ? QDir::homePath()
                    : xyModemDirectory_;
    QFileDialog dialog(this);
    if (protocol == XyModemTransfer::Protocol::Xmodem) {
        dialog.setWindowTitle(
                tr("选择 XMODEM 保存文件"));
        dialog.setDirectory(initialDirectory);
        dialog.selectFile(
                QStringLiteral("xmodem-download.bin"));
        dialog.setNameFilter(tr("所有文件 (*)"));
        dialog.setAcceptMode(QFileDialog::AcceptSave);
        dialog.setFileMode(QFileDialog::AnyFile);
    } else {
        dialog.setWindowTitle(
                tr("选择 YMODEM 下载目录"));
        dialog.setDirectory(initialDirectory);
        dialog.setAcceptMode(QFileDialog::AcceptOpen);
        dialog.setFileMode(QFileDialog::Directory);
        dialog.setOption(QFileDialog::ShowDirsOnly);
    }
    if (!executeXyModemFileDialog(&dialog)) {
        return {};
    }
    return dialog.selectedFiles().value(0);
}

bool BaseTerminal::executeXyModemFileDialog(
        QFileDialog *dialog) {
    if (dialog == nullptr || xyModemFileDialog_ != nullptr) {
        return false;
    }
    xyModemFileDialog_ = dialog;
    const bool accepted =
            dialog->exec() == QDialog::Accepted;
    xyModemFileDialog_ = nullptr;
    return accepted;
}

void BaseTerminal::startPendingXyModemTransfer() {
    const XyModemCommand command =
            pendingXyModemCommand_;
    if (command == XyModemCommand::None) {
        return;
    }
    if (!isConnect() || xyModemTransfer_->isActive() || zmodemTransfer_->isActive()) {
        clearPendingXyModemCommand();
        return;
    }

    pendingXyModemTimer_.stop();
    pendingXyModemDialogScheduled_ = true;
    const XyModemTransfer::Protocol protocol =
            xyModemProtocol(command);
    if (isXyModemSendCommand(command)) {
        const QStringList files =
                selectXyModemSendFiles(protocol);
        if (pendingXyModemCommand_ != command) {
            return;
        }
        if (files.isEmpty()) {
            clearPendingXyModemCommand();
            writeToBackend(XyModem::cancelSequence());
            return;
        }

        const QByteArray protocolData =
                pendingXyModemProtocolData_;
        xyModemDirectory_ =
                QFileInfo(files.constFirst()).absolutePath();
        clearPendingXyModemCommand();
        xyModemTransfer_->send(protocol, files);
        if (xyModemTransfer_->isActive()) {
            const QByteArray terminalData =
                    xyModemTransfer_->consume(protocolData);
            if (!terminalData.isEmpty()) {
                recvData(
                        terminalData.constData(),
                        static_cast<int>(
                                terminalData.size()));
            }
        }
        return;
    }

    const QString destination =
            selectXyModemReceiveDestination(protocol);
    if (pendingXyModemCommand_ != command) {
        return;
    }
    if (destination.isEmpty()) {
        clearPendingXyModemCommand();
        writeToBackend(XyModem::cancelSequence());
        return;
    }

    xyModemDirectory_ =
            protocol == XyModemTransfer::Protocol::Xmodem
                    ? QFileInfo(destination).absolutePath()
                    : destination;
    clearPendingXyModemCommand();
    xyModemTransfer_->receive(protocol, destination);
}

void BaseTerminal::onXyModemFileStarted(
        const XyModemTransfer::Protocol protocol,
        const XyModemTransfer::Direction direction,
        const QString &fileName,
        const qint64 size,
        const int fileNumber,
        const int fileCount) {
    closeXyModemProgress();

    const QString action =
            direction == XyModemTransfer::Direction::Download
                    ? tr("正在下载")
                    : tr("正在上传");
    const QString protocolName = xyModemName(protocol);
    xyModemProgress_ = new QProgressDialog(this);
    xyModemProgress_->setWindowTitle(
            tr("%1 文件传输").arg(protocolName));
    if (fileCount > 0) {
        xyModemProgress_->setLabelText(
                tr("%1 %2（%3/%4）")
                        .arg(action, fileName)
                        .arg(fileNumber)
                        .arg(fileCount));
    } else {
        xyModemProgress_->setLabelText(
                tr("%1 %2（第 %3 个文件）")
                        .arg(action, fileName)
                        .arg(fileNumber));
    }
    xyModemProgress_->setCancelButtonText(tr("取消"));
    xyModemProgress_->setRange(0, size <= 0 ? 0 : 1000);
    xyModemProgress_->setValue(0);
    xyModemProgress_->setMinimumDuration(0);
    xyModemProgress_->setAutoClose(false);
    xyModemProgress_->setAutoReset(false);
    xyModemProgress_->setWindowModality(Qt::WindowModal);
    QObject::connect(xyModemProgress_,
                     &QProgressDialog::canceled,
                     xyModemTransfer_,
                     &XyModemTransfer::cancel);
    xyModemProgress_->show();
}

void BaseTerminal::onXyModemFileProgress(
        XyModemTransfer::Protocol,
        XyModemTransfer::Direction,
        const QString &,
        const qint64 transferred,
        const qint64 size) {
    if (xyModemProgress_ == nullptr) {
        return;
    }
    if (size <= 0) {
        xyModemProgress_->setRange(0, 0);
        return;
    }
    const qint64 boundedTransferred =
            std::clamp<qint64>(transferred, 0, size);
    const int progress =
            static_cast<int>(
                    (boundedTransferred * 1000) / size);
    xyModemProgress_->setRange(0, 1000);
    xyModemProgress_->setValue(progress);
}

void BaseTerminal::closeXyModemProgress() {
    if (xyModemProgress_ == nullptr) {
        return;
    }
    QObject::disconnect(xyModemProgress_,
                        nullptr,
                        xyModemTransfer_,
                        nullptr);
    xyModemProgress_->close();
    xyModemProgress_->deleteLater();
    xyModemProgress_ = nullptr;
}

void BaseTerminal::populateFileTransferMenu(QMenu *menu) {
    const bool canStartFileTransfer =
            isConnect() && pendingXyModemCommand_ == XyModemCommand::None && !xyModemTransfer_->isActive() && !zmodemTransfer_->isActive();

    QAction *sendXmodemAction =
            menu->addAction(
                    tr("通过 XMODEM 发送文件..."));
    sendXmodemAction->setEnabled(canStartFileTransfer);
    QObject::connect(
            sendXmodemAction,
            &QAction::triggered,
            this,
            [this]() {
                startXyModemSend(
                        XyModemTransfer::Protocol::Xmodem);
            });

    QAction *receiveXmodemAction =
            menu->addAction(
                    tr("通过 XMODEM 接收文件..."));
    receiveXmodemAction->setEnabled(canStartFileTransfer);
    QObject::connect(
            receiveXmodemAction,
            &QAction::triggered,
            this,
            [this]() {
                startXyModemReceive(
                        XyModemTransfer::Protocol::Xmodem);
            });

    menu->addSeparator();

    QAction *sendYmodemAction =
            menu->addAction(
                    tr("通过 YMODEM 发送文件..."));
    sendYmodemAction->setEnabled(canStartFileTransfer);
    QObject::connect(
            sendYmodemAction,
            &QAction::triggered,
            this,
            [this]() {
                startXyModemSend(
                        XyModemTransfer::Protocol::Ymodem);
            });

    QAction *receiveYmodemAction =
            menu->addAction(
                    tr("通过 YMODEM 接收文件..."));
    receiveYmodemAction->setEnabled(canStartFileTransfer);
    QObject::connect(
            receiveYmodemAction,
            &QAction::triggered,
            this,
            [this]() {
                startXyModemReceive(
                        XyModemTransfer::Protocol::Ymodem);
            });

    if (xyModemTransfer_->isActive()) {
        menu->addSeparator();
        QAction *cancelTransferAction =
                menu->addAction(
                        tr("取消当前 X/YMODEM 传输"));
        QObject::connect(
                cancelTransferAction,
                &QAction::triggered,
                xyModemTransfer_,
                &XyModemTransfer::cancel);
    }
}

void BaseTerminal::contextMenuEvent(QContextMenuEvent *event) {
    QMenu menu(this);

    // 复制操作
    QAction *copyAction = menu.addAction(tr("复制"));
    copyAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
    copyAction->setEnabled(selectedText().length() > 0);
    QObject::connect(copyAction, &QAction::triggered, this, [this]() {
        copyClipboard();
    });

    // 粘贴操作
    QAction *pasteAction = menu.addAction(tr("粘贴"));
    pasteAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
    QObject::connect(pasteAction, &QAction::triggered, this, [this]() {
        pasteClipboard();
    });

    menu.addSeparator();

    // 高亮菜单
    QMenu *highlightMenu = menu.addMenu(tr("高亮"));
    highlightMenu->setIcon(QIcon::fromTheme("edit-select-all"));
    buildHighlightMenu(highlightMenu);

    menu.addSeparator();

    // 日志保存操作
    if (logging_) {
        QAction *logAction = menu.addAction(tr("停止保存日志"));
        logAction->setIcon(QIcon::fromTheme("media-playback-stop"));
        QObject::connect(logAction, &QAction::triggered, this, [this]() {
            onToggleLogging(false);
        });
    } else {
        QMenu *logMenu = menu.addMenu(tr("保存日志"));
        logMenu->setIcon(QIcon::fromTheme("document-save"));

        QAction *fromNowAction = logMenu->addAction(tr("仅保存后续日志"));
        QObject::connect(fromNowAction, &QAction::triggered, this, [this]() {
            onToggleLogging(false);
        });

        QAction *includeBufferedLogsAction = logMenu->addAction(tr("保存已有及后续日志"));
        QObject::connect(includeBufferedLogsAction, &QAction::triggered, this, [this]() {
            onToggleLogging(true);
        });
    }

    menu.addSeparator();

    // 清屏操作
    QAction *clearAction = menu.addAction(tr("清屏"));
    clearAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_X));
    QObject::connect(clearAction, &QAction::triggered, this, [this]() {
        clear();
    });

    menu.exec(event->globalPos());
}

void BaseTerminal::buildHighlightMenu(QMenu *parentMenu) {
    QString selected = selectedText().trimmed();
    bool hasSelection = !selected.isEmpty();
    QMap<QString, QColor> highlights = getHighLightTexts();

    if (hasSelection) {
        // 高亮（随机颜色）
        QAction *highlightRandomAction = parentMenu->addAction(tr("高亮（随机颜色）"));
        highlightRandomAction->setIcon(QIcon::fromTheme("color-picker"));
        QObject::connect(highlightRandomAction, &QAction::triggered, this, [this, selected]() {
            QColor randomColor = generateRandomColor();
            addHighLightText(selected, randomColor);
        });

        // 高亮（自定义）
        QAction *highlightCustomAction = parentMenu->addAction(tr("高亮（自定义）..."));
        highlightCustomAction->setIcon(QIcon::fromTheme("color-management"));
        QObject::connect(highlightCustomAction, &QAction::triggered, this, [this, selected]() {
            QColor initialColor = Qt::yellow;
            // 如果已经有高亮，使用当前颜色作为初始颜色
            if (isContainHighLightText(selected)) {
                QMap<QString, QColor> highlightList = getHighLightTexts();
                if (highlightList.contains(selected)) {
                    initialColor = highlightList.value(selected);
                }
            }
            QColor color = QColorDialog::getColor(initialColor, this, tr("选择高亮颜色"));
            if (color.isValid()) {
                addHighLightText(selected, color);
            }
        });

        // 取消高亮（仅当选中文本已被高亮时显示）
        if (isContainHighLightText(selected)) {
            QAction *removeHighlightAction = parentMenu->addAction(tr("取消高亮"));
            removeHighlightAction->setIcon(QIcon::fromTheme("edit-clear"));
            QObject::connect(removeHighlightAction, &QAction::triggered, this, [this, selected]() {
                removeHighLightText(selected);
            });
        }

        parentMenu->addSeparator();
    }

    // 清除所有高亮
    QAction *clearHighlightAction = parentMenu->addAction(tr("清除所有高亮"));
    clearHighlightAction->setIcon(QIcon::fromTheme("edit-clear-all"));
    clearHighlightAction->setEnabled(!highlights.isEmpty());
    QObject::connect(clearHighlightAction, &QAction::triggered, this, [this]() {
        clearHighLightTexts();
    });

    // 如果有高亮项，直接列出
    if (!highlights.isEmpty()) {
        parentMenu->addSeparator();

        for (auto it = highlights.begin(); it != highlights.end(); ++it) {
            const QString &text = it.key();
            const QColor &color = it.value();

            // 截断过长的文本用于显示
            QString displayText = text;
            if (displayText.length() > 30) {
                displayText = displayText.left(27) + "...";
            }

            // 为每个高亮项创建子菜单
            QMenu *itemMenu = parentMenu->addMenu(displayText);

            // 创建颜色图标
            QPixmap pixmap(16, 16);
            pixmap.fill(color);
            itemMenu->setIcon(QIcon(pixmap));

            // 显示完整文本（如果被截断）
            if (text != displayText) {
                QAction *fullTextAction = itemMenu->addAction(tr("文本: %1").arg(text));
                fullTextAction->setEnabled(false);
                itemMenu->addSeparator();
            }

            // 删除选项
            QAction *deleteAction = itemMenu->addAction(tr("删除"));
            deleteAction->setIcon(QIcon::fromTheme("edit-delete"));
            QObject::connect(deleteAction, &QAction::triggered, this, [this, text]() {
                removeHighLightText(text);
            });

            // 更改颜色选项
            QAction *changeColorAction = itemMenu->addAction(tr("更改颜色..."));
            changeColorAction->setIcon(QIcon::fromTheme("color-picker"));
            QObject::connect(changeColorAction, &QAction::triggered, this, [this, text, color]() {
                QColor newColor = QColorDialog::getColor(color, this, tr("选择新的高亮颜色"));
                if (newColor.isValid()) {
                    removeHighLightText(text);
                    addHighLightText(text, newColor);
                }
            });
        }
    }
}

QColor BaseTerminal::generateRandomColor() {
    // 生成饱和度和亮度较高的随机颜色，确保可见性好
    int hue = QRandomGenerator::global()->bounded(360);
    int saturation = 150 + QRandomGenerator::global()->bounded(106);  // 150-255
    int value = 180 + QRandomGenerator::global()->bounded(76);        // 180-255
    return QColor::fromHsv(hue, saturation, value);
}

void BaseTerminal::onToggleLogging(bool includeBufferedLogs) {
    if (logging_) {
        // 停止日志记录
        stopLogging();
        QMessageBox::information(this, tr("日志保存"),
            tr("日志保存已停止。\n文件: %1").arg(logFilePath_));
    } else {
        // 开始日志记录 - 打开文件选择对话框
        QString defaultFileName = QString("terminal_log_%1.log")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));

        QString filePath = QFileDialog::getSaveFileName(
            this,
            tr("保存终端日志"),
            QDir::homePath() + "/" + defaultFileName,
            tr("日志文件 (*.log *.txt);;所有文件 (*)"),
            nullptr,
            QFileDialog::DontConfirmOverwrite  // 允许选择现有文件
        );

        if (!filePath.isEmpty()) {
            // 如果文件已存在，询问是追加还是覆盖
            if (QFile::exists(filePath)) {
                QMessageBox::StandardButton reply = QMessageBox::question(
                    this,
                    tr("文件已存在"),
                    tr("文件 \"%1\" 已存在。\n\n"
                       "点击\"是\"追加到现有文件\n"
                       "点击\"否\"覆盖现有文件\n"
                       "点击\"取消\"放弃操作").arg(QFileInfo(filePath).fileName()),
                    QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
                    QMessageBox::Yes
                );

                if (reply == QMessageBox::Cancel) {
                    return;
                }

                if (reply == QMessageBox::No) {
                    // 覆盖文件 - 先删除
                    QFile::remove(filePath);
                }
            }

            startLogging(filePath, includeBufferedLogs);

            if (logging_) {
                QMessageBox::information(this, tr("日志保存"),
                    tr("开始保存日志到:\n%1").arg(filePath));
            }
        }
    }
}

void BaseTerminal::startLogging(const QString &filePath, bool includeBufferedLogs) {
    if (logging_) {
        stopLogging();
    }

    logFile_ = new QFile(filePath);

    // 以追加模式打开（如果用户选择覆盖，文件已被删除）
    if (!logFile_->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QMessageBox::critical(this, tr("错误"),
            tr("无法打开文件进行写入:\n%1\n\n错误: %2")
                .arg(filePath)
                .arg(logFile_->errorString()));
        delete logFile_;
        logFile_ = nullptr;
        return;
    }

    logFilePath_ = filePath;
    logging_ = true;

    // 写入日志头
    QString header = QString("\n========== 日志开始: %1 ==========\n")
        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
    logFile_->write(header.toUtf8());

    if (includeBufferedLogs) {
        const int bufferedLineCount = historyLinesCount() + screenLinesCount();
        if (bufferedLineCount > 0) {
            QString bufferedLogs;
            QTextStream stream(&bufferedLogs, QIODevice::WriteOnly);
            saveHistory(&stream, 0, 0, bufferedLineCount - 1);
            stream.flush();

            // 终端屏幕未使用的行也在缓存中，避免将它们写成文件尾部的大量空行。
            while (bufferedLogs.endsWith('\n') || bufferedLogs.endsWith('\r')) {
                bufferedLogs.chop(1);
            }
            if (!bufferedLogs.isEmpty()) {
                logFile_->write(bufferedLogs.toUtf8());
                logFile_->write("\n");
            }
        }
    }

    logFile_->flush();

    emit loggingStateChanged(true);

    qDebug() << "Started logging to:" << filePath;
}

void BaseTerminal::stopLogging() {
    if (!logging_ || !logFile_) {
        return;
    }

    // 写入日志尾
    QString footer = QString("\n========== 日志结束: %1 ==========\n")
        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
    logFile_->write(footer.toUtf8());
    logFile_->flush();

    logFile_->close();
    delete logFile_;
    logFile_ = nullptr;
    logging_ = false;

    emit loggingStateChanged(false);

    qDebug() << "Stopped logging to:" << logFilePath_;
}

void BaseTerminal::writeToLog(const QString &line) {
    if (!logFile_ || !logFile_->isOpen()) {
        return;
    }

    if (ConfigManager::instance()->globalSettings().logTimestamp) {
        const QString timestamp = QDateTime::currentDateTime().toString("[yyyy-MM-dd HH:mm:ss.zzz] ");
        logFile_->write(timestamp.toUtf8());
    }

    logFile_->write(line.toUtf8());
    logFile_->write("\n");

    // 定期刷新确保数据写入磁盘
    static int writeCount = 0;
    if (++writeCount >= 10) {  // 每10次写入刷新一次
        logFile_->flush();
        writeCount = 0;
    }
}
