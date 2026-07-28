#include "core/xymodem/XyModemProtocol.h"
#include "core/xymodem/XyModemTransfer.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QMetaObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>

namespace {

bool writeFile(const QString &path, const QByteArray &data) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qCritical() << "cannot create" << path << file.errorString();
        return false;
    }
    if (file.write(data) != data.size()) {
        qCritical() << "cannot write" << path << file.errorString();
        return false;
    }
    return true;
}

QByteArray readFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qCritical() << "cannot read" << path << file.errorString();
        return {};
    }
    return file.readAll();
}

QByteArray testPayload(const int size) {
    QByteArray data;
    data.reserve(size);
    for (int index = 0; index < size; ++index) {
        data.append(static_cast<char>((index * 37 + 11) & 0xff));
    }
    if (!data.isEmpty() && data.endsWith(XyModem::padding)) {
        data[data.size() - 1] = '\x7f';
    }
    return data;
}

bool runUntil(const std::function<bool()> &done,
              const int timeoutMilliseconds) {
    if (done()) {
        return true;
    }

    QEventLoop loop;
    QTimer completionTimer;
    completionTimer.setInterval(10);
    QObject::connect(&completionTimer,
                     &QTimer::timeout,
                     &loop,
                     [&loop, &done]() {
                         if (done()) {
                             loop.quit();
                         }
                     });
    completionTimer.start();
    QTimer::singleShot(timeoutMilliseconds,
                       &loop,
                       &QEventLoop::quit);
    loop.exec();
    return done();
}

void deliverFragmented(XyModemTransfer *target,
                       const QByteArray &data,
                       QString *failure) {
    qsizetype offset = 0;
    int chunkSize = 1;
    while (offset < data.size()) {
        const QByteArray terminalData =
                target->consume(data.mid(offset, chunkSize));
        if (!terminalData.isEmpty() && failure->isEmpty()) {
            *failure = QStringLiteral(
                    "unexpected terminal data during transfer");
        }
        offset += chunkSize;
        chunkSize = chunkSize == 19 ? 1 : chunkSize + 1;
    }
}

struct ProcessTransferState {
    QString failure;
    bool transferFinished = false;
    bool processFinished = false;
};

void connectProcessTransfer(QProcess *process,
                            XyModemTransfer *transfer,
                            ProcessTransferState *state,
                            int expectedFileCount) {
    QObject::connect(process,
                     &QProcess::readyReadStandardOutput,
                     transfer,
                     [process, transfer]() {
                         transfer->consume(
                                 process->readAllStandardOutput());
                     });
    QObject::connect(process,
                     &QProcess::readyReadStandardError,
                     process,
                     [process]() {
                         process->readAllStandardError();
                     });
    QObject::connect(transfer,
                     &XyModemTransfer::outboundData,
                     process,
                     [process](const QByteArray &data) {
                         process->write(data);
                     });
    QObject::connect(
            transfer,
            &XyModemTransfer::transferFinished,
            transfer,
            [state, expectedFileCount](
                    XyModemTransfer::Protocol,
                    XyModemTransfer::Direction,
                    int count) {
                if (count != expectedFileCount) {
                    state->failure =
                            QStringLiteral(
                                    "invalid transferred file count");
                    return;
                }
                state->transferFinished = true;
            });
    QObject::connect(
            transfer,
            &XyModemTransfer::transferFailed,
            transfer,
            [state](XyModemTransfer::Protocol,
                    XyModemTransfer::Direction,
                    const QString &message) {
                state->failure = message;
            });
    QObject::connect(
            process,
            qOverload<int, QProcess::ExitStatus>(
                    &QProcess::finished),
            process,
            [state](int exitCode,
                    QProcess::ExitStatus exitStatus) {
                state->processFinished = true;
                if (exitStatus != QProcess::NormalExit || exitCode != 0) {
                    state->failure =
                            QStringLiteral(
                                    "transfer tool exited with code %1")
                                    .arg(exitCode);
                }
            });
}

bool waitForProcessTransfer(ProcessTransferState *state,
                            QProcess *process,
                            const QString &description) {
    const bool completed = runUntil(
            [state]() {
                return !state->failure.isEmpty() || (state->transferFinished && state->processFinished);
            },
            20000);
    if (!completed || !state->failure.isEmpty()) {
        qCritical() << description
                    << (state->failure.isEmpty()
                                ? QStringLiteral("timeout")
                                : state->failure);
        process->kill();
        process->waitForFinished();
        return false;
    }
    return true;
}

bool testProtocolCodec() {
    if (XyModem::crc16(QByteArrayLiteral("123456789")) != 0x31c3U) {
        qCritical() << "XMODEM CRC vector mismatch";
        return false;
    }

    const QByteArray payload = testPayload(83);
    for (const XyModem::CheckMode checkMode:
         {XyModem::CheckMode::Checksum,
          XyModem::CheckMode::Crc16}) {
        for (const int blockSize:
             {XyModem::shortBlockSize,
              XyModem::longBlockSize}) {
            const QByteArray encoded =
                    XyModem::encodePacket(
                            0x5aU,
                            payload,
                            blockSize,
                            checkMode);
            XyModem::Packet decoded;
            if (!XyModem::decodePacket(
                        encoded, checkMode, &decoded) ||
                decoded.number != 0x5aU || !decoded.data.startsWith(payload) || decoded.data.size() != blockSize) {
                qCritical() << "X/YMODEM packet round trip failed";
                return false;
            }

            QByteArray corrupted = encoded;
            corrupted[10] =
                    static_cast<char>(corrupted.at(10) ^ 0x01);
            if (XyModem::decodePacket(
                        corrupted, checkMode, &decoded)) {
                qCritical() << "corrupt X/YMODEM packet accepted";
                return false;
            }
        }
    }
    return true;
}

bool testXmodemChecksumUpload() {
    QTemporaryDir sourceDirectory;
    if (!sourceDirectory.isValid()) {
        return false;
    }
    const QByteArray payload = testPayload(4099);
    const QString sourcePath =
            sourceDirectory.filePath(
                    QStringLiteral("checksum.bin"));
    if (!writeFile(sourcePath, payload)) {
        return false;
    }

    XyModemTransfer sender;
    QList<QByteArray> packets;
    bool finished = false;
    QString failure;
    QObject::connect(&sender,
                     &XyModemTransfer::outboundData,
                     &sender,
                     [&packets](const QByteArray &data) {
                         packets.append(data);
                     });
    QObject::connect(
            &sender,
            &XyModemTransfer::transferFinished,
            &sender,
            [&finished](XyModemTransfer::Protocol protocol,
                        XyModemTransfer::Direction direction,
                        int count) {
                finished =
                        protocol == XyModemTransfer::Protocol::Xmodem && direction == XyModemTransfer::Direction::Upload && count == 1;
            });
    QObject::connect(
            &sender,
            &XyModemTransfer::transferFailed,
            &sender,
            [&failure](XyModemTransfer::Protocol,
                       XyModemTransfer::Direction,
                       const QString &message) {
                failure = message;
            });

    sender.send(XyModemTransfer::Protocol::Xmodem,
                {sourcePath});
    sender.consume(QByteArray(1, XyModem::nak));

    QByteArray received;
    int packetIndex = 0;
    while (sender.isActive() && packetIndex < packets.size() && failure.isEmpty()) {
        const QByteArray &packet = packets.at(packetIndex++);
        if (packet == QByteArray(1, XyModem::eot)) {
            sender.consume(QByteArray(1, XyModem::ack));
            continue;
        }
        XyModem::Packet decoded;
        if (!XyModem::decodePacket(
                    packet,
                    XyModem::CheckMode::Checksum,
                    &decoded)) {
            qCritical() << "invalid checksum upload packet";
            return false;
        }
        received.append(decoded.data);
        sender.consume(QByteArray(1, XyModem::ack));
    }

    if (!failure.isEmpty() || !finished || received.left(payload.size()) != payload) {
        qCritical() << "XMODEM checksum upload failed"
                    << failure;
        return false;
    }
    return true;
}

bool testXmodemReceivers() {
    QTemporaryDir destinationDirectory;
    if (!destinationDirectory.isValid()) {
        return false;
    }

    const QByteArray checksumPayload = testPayload(311);
    const QString checksumPath =
            destinationDirectory.filePath(
                    QStringLiteral("checksum-download.bin"));
    XyModemTransfer checksumReceiver;
    QByteArray checksumResponses;
    bool checksumFinished = false;
    QObject::connect(
            &checksumReceiver,
            &XyModemTransfer::outboundData,
            &checksumReceiver,
            [&checksumResponses](const QByteArray &data) {
                checksumResponses.append(data);
            });
    QObject::connect(
            &checksumReceiver,
            &XyModemTransfer::transferFinished,
            &checksumReceiver,
            [&checksumFinished](
                    XyModemTransfer::Protocol,
                    XyModemTransfer::Direction,
                    int count) {
                checksumFinished = count == 1;
            });

    checksumReceiver.receive(
            XyModemTransfer::Protocol::Xmodem,
            checksumPath);
    for (int request = 0; request < 3; ++request) {
        QMetaObject::invokeMethod(
                &checksumReceiver,
                "onTimeout",
                Qt::DirectConnection);
    }
    if (!checksumResponses.endsWith(XyModem::nak)) {
        qCritical() << "XMODEM checksum fallback was not requested";
        return false;
    }

    quint8 blockNumber = 1;
    for (qsizetype offset = 0;
         offset < checksumPayload.size();
         offset += XyModem::shortBlockSize) {
        checksumReceiver.consume(
                XyModem::encodePacket(
                        blockNumber++,
                        checksumPayload.mid(
                                offset,
                                XyModem::shortBlockSize),
                        XyModem::shortBlockSize,
                        XyModem::CheckMode::Checksum));
    }
    checksumReceiver.consume(
            QByteArray(1, XyModem::eot));
    if (!checksumFinished || readFile(checksumPath) != checksumPayload) {
        qCritical() << "XMODEM checksum download failed";
        return false;
    }

    const QByteArray oneKilobytePayload =
            testPayload(777);
    const QString oneKilobytePath =
            destinationDirectory.filePath(
                    QStringLiteral("xmodem-1k.bin"));
    XyModemTransfer oneKilobyteReceiver;
    bool oneKilobyteFinished = false;
    QObject::connect(
            &oneKilobyteReceiver,
            &XyModemTransfer::transferFinished,
            &oneKilobyteReceiver,
            [&oneKilobyteFinished](
                    XyModemTransfer::Protocol,
                    XyModemTransfer::Direction,
                    int count) {
                oneKilobyteFinished = count == 1;
            });
    oneKilobyteReceiver.receive(
            XyModemTransfer::Protocol::Xmodem,
            oneKilobytePath);
    oneKilobyteReceiver.consume(
            XyModem::encodePacket(
                    1,
                    oneKilobytePayload,
                    XyModem::longBlockSize,
                    XyModem::CheckMode::Crc16));
    const QByteArray terminalOutput =
            oneKilobyteReceiver.consume(
                    QByteArray(1, XyModem::eot) + QByteArrayLiteral("prompt"));
    if (!oneKilobyteFinished || readFile(oneKilobytePath) != oneKilobytePayload || terminalOutput != QByteArrayLiteral("prompt")) {
        qCritical() << "XMODEM-1K download failed";
        return false;
    }
    return true;
}

bool testYmodemInMemoryTransfer() {
    QTemporaryDir sourceDirectory;
    QTemporaryDir destinationDirectory;
    if (!sourceDirectory.isValid() || !destinationDirectory.isValid()) {
        return false;
    }

    const QByteArray payload = testPayload(32791);
    const QString binarySource =
            sourceDirectory.filePath(
                    QStringLiteral("all-bytes.bin"));
    const QString emptySource =
            sourceDirectory.filePath(
                    QStringLiteral("empty.bin"));
    if (!writeFile(binarySource, payload) || !writeFile(emptySource, {})) {
        return false;
    }

    const QString existingDestination =
            destinationDirectory.filePath(
                    QStringLiteral("all-bytes.bin"));
    const QByteArray existingData("keep-existing");
    if (!writeFile(existingDestination, existingData)) {
        return false;
    }

    XyModemTransfer sender;
    XyModemTransfer receiver;
    QString failure;
    bool senderFinished = false;
    bool receiverFinished = false;
    bool corruptedPacket = false;
    QStringList downloadedPaths;

    QObject::connect(
            &sender,
            &XyModemTransfer::outboundData,
            &receiver,
            [&receiver, &failure, &corruptedPacket](
                    const QByteArray &data) {
                QByteArray forwarded = data;
                if (!corruptedPacket && forwarded.size() > XyModem::shortBlockSize && forwarded.at(0) == XyModem::stx && static_cast<quint8>(forwarded.at(1)) == 1U) {
                    forwarded[20] = static_cast<char>(
                            forwarded.at(20) ^ 0x01);
                    corruptedPacket = true;
                }
                QTimer::singleShot(
                        0,
                        &receiver,
                        [&receiver, &failure, forwarded]() {
                            deliverFragmented(
                                    &receiver,
                                    forwarded,
                                    &failure);
                        });
            });
    QObject::connect(
            &receiver,
            &XyModemTransfer::outboundData,
            &sender,
            [&sender, &failure](const QByteArray &data) {
                QTimer::singleShot(
                        0,
                        &sender,
                        [&sender, &failure, data]() {
                            deliverFragmented(
                                    &sender,
                                    data,
                                    &failure);
                        });
            });
    QObject::connect(
            &receiver,
            &XyModemTransfer::fileFinished,
            &receiver,
            [&downloadedPaths](
                    XyModemTransfer::Protocol,
                    XyModemTransfer::Direction,
                    const QString &path) {
                downloadedPaths.append(path);
            });
    QObject::connect(
            &sender,
            &XyModemTransfer::transferFinished,
            &sender,
            [&senderFinished, &failure](
                    XyModemTransfer::Protocol protocol,
                    XyModemTransfer::Direction direction,
                    int count) {
                senderFinished = true;
                if (protocol != XyModemTransfer::Protocol::Ymodem || direction != XyModemTransfer::Direction::Upload || count != 2) {
                    failure = QStringLiteral(
                            "invalid YMODEM sender completion");
                }
            });
    QObject::connect(
            &receiver,
            &XyModemTransfer::transferFinished,
            &receiver,
            [&receiverFinished, &failure](
                    XyModemTransfer::Protocol protocol,
                    XyModemTransfer::Direction direction,
                    int count) {
                receiverFinished = true;
                if (protocol != XyModemTransfer::Protocol::Ymodem || direction != XyModemTransfer::Direction::Download || count != 2) {
                    failure = QStringLiteral(
                            "invalid YMODEM receiver completion");
                }
            });
    const auto recordFailure =
            [&failure](XyModemTransfer::Protocol,
                       XyModemTransfer::Direction,
                       const QString &message) {
                if (failure.isEmpty()) {
                    failure = message;
                }
            };
    QObject::connect(
            &sender,
            &XyModemTransfer::transferFailed,
            &sender,
            recordFailure);
    QObject::connect(
            &receiver,
            &XyModemTransfer::transferFailed,
            &receiver,
            recordFailure);

    sender.send(XyModemTransfer::Protocol::Ymodem,
                {binarySource, emptySource});
    receiver.receive(
            XyModemTransfer::Protocol::Ymodem,
            destinationDirectory.path());
    const bool completed = runUntil(
            [&]() {
                return !failure.isEmpty() || (senderFinished && receiverFinished);
            },
            10000);
    if (!completed || !failure.isEmpty() || !corruptedPacket) {
        qCritical() << "in-memory YMODEM transfer failed"
                    << (failure.isEmpty()
                                ? QStringLiteral("timeout")
                                : failure);
        return false;
    }
    if (readFile(existingDestination) != existingData || downloadedPaths.size() != 2 || readFile(downloadedPaths.at(0)) != payload || !readFile(downloadedPaths.at(1)).isEmpty()) {
        qCritical() << "in-memory YMODEM contents mismatch"
                    << downloadedPaths;
        return false;
    }
    return true;
}

bool testCanceledDownloadCleanup() {
    QTemporaryDir destinationDirectory;
    if (!destinationDirectory.isValid()) {
        return false;
    }

    const QString targetPath =
            destinationDirectory.filePath(
                    QStringLiteral("existing.bin"));
    const QByteArray existingData("existing");
    if (!writeFile(targetPath, existingData)) {
        return false;
    }

    XyModemTransfer receiver;
    bool canceled = false;
    QObject::connect(
            &receiver,
            &XyModemTransfer::transferCanceled,
            &receiver,
            [&canceled](XyModemTransfer::Protocol,
                        XyModemTransfer::Direction) {
                canceled = true;
            });
    receiver.receive(
            XyModemTransfer::Protocol::Xmodem,
            targetPath);
    receiver.consume(XyModem::encodePacket(
            1,
            QByteArrayLiteral("partial"),
            XyModem::shortBlockSize,
            XyModem::CheckMode::Crc16));
    receiver.cancel();

    const QStringList remainingFiles =
            QDir(destinationDirectory.path())
                    .entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
    if (!canceled || readFile(targetPath) != existingData || remainingFiles != QStringList{QStringLiteral("existing.bin")}) {
        qCritical() << "canceled XMODEM download left data"
                    << remainingFiles;
        return false;
    }
    return true;
}

bool testLrzszXmodemDownload(const QString &sxExecutable) {
    QTemporaryDir sourceDirectory;
    QTemporaryDir destinationDirectory;
    if (!sourceDirectory.isValid() || !destinationDirectory.isValid()) {
        return false;
    }

    const QByteArray payload = testPayload(12307);
    const QString sourcePath =
            sourceDirectory.filePath(QStringLiteral("from-sx.bin"));
    const QString destinationPath =
            destinationDirectory.filePath(QStringLiteral("from-sx.bin"));
    if (!writeFile(sourcePath, payload)) {
        return false;
    }

    XyModemTransfer receiver;
    QProcess process;
    process.setProgram(sxExecutable);
    process.setArguments(
            {QStringLiteral("--xmodem"),
             QStringLiteral("--binary"),
             sourcePath});
    ProcessTransferState state;
    connectProcessTransfer(&process, &receiver, &state, 1);

    process.start();
    if (!process.waitForStarted(3000)) {
        qCritical() << "cannot start sx" << process.errorString();
        return false;
    }
    receiver.receive(XyModemTransfer::Protocol::Xmodem,
                     destinationPath);
    if (!waitForProcessTransfer(
                &state,
                &process,
                QStringLiteral("sx interoperability failed"))) {
        return false;
    }
    if (readFile(destinationPath) != payload) {
        qCritical() << "sx download contents mismatch";
        return false;
    }
    return true;
}

bool testLrzszXmodemUpload(const QString &rxExecutable,
                           const bool requestCrc) {
    QTemporaryDir sourceDirectory;
    QTemporaryDir destinationDirectory;
    if (!sourceDirectory.isValid() || !destinationDirectory.isValid()) {
        return false;
    }

    const QByteArray payload = testPayload(12288);
    const QString sourcePath =
            sourceDirectory.filePath(QStringLiteral("to-rx.bin"));
    if (!writeFile(sourcePath, payload)) {
        return false;
    }

    XyModemTransfer sender;
    QProcess process;
    process.setProgram(rxExecutable);
    QStringList arguments{
            QStringLiteral("--xmodem"),
            QStringLiteral("--binary")};
    if (requestCrc) {
        arguments.append(QStringLiteral("--with-crc"));
    }
    arguments.append(QStringLiteral("to-rx.bin"));
    process.setArguments(arguments);
    process.setWorkingDirectory(destinationDirectory.path());
    ProcessTransferState state;
    connectProcessTransfer(&process, &sender, &state, 1);

    process.start();
    if (!process.waitForStarted(3000)) {
        qCritical() << "cannot start rx" << process.errorString();
        return false;
    }
    sender.send(XyModemTransfer::Protocol::Xmodem,
                {sourcePath});
    if (!waitForProcessTransfer(
                &state,
                &process,
                requestCrc
                        ? QStringLiteral("rx CRC interoperability failed")
                        : QStringLiteral("rx checksum interoperability failed"))) {
        return false;
    }

    const QString receivedPath =
            destinationDirectory.filePath(QStringLiteral("to-rx.bin"));
    if (readFile(receivedPath) != payload) {
        qCritical() << "rx upload contents mismatch" << requestCrc;
        return false;
    }
    return true;
}

bool testLrzszYmodemDownload(const QString &sbExecutable) {
    QTemporaryDir sourceDirectory;
    QTemporaryDir destinationDirectory;
    if (!sourceDirectory.isValid() || !destinationDirectory.isValid()) {
        return false;
    }

    const QByteArray firstPayload = testPayload(17003);
    const QByteArray secondPayload = testPayload(513);
    const QString firstSource =
            sourceDirectory.filePath(QStringLiteral("from-sb-one.bin"));
    const QString secondSource =
            sourceDirectory.filePath(QStringLiteral("from-sb-two.bin"));
    if (!writeFile(firstSource, firstPayload) || !writeFile(secondSource, secondPayload)) {
        return false;
    }

    XyModemTransfer receiver;
    QProcess process;
    process.setProgram(sbExecutable);
    process.setArguments(
            {QStringLiteral("--ymodem"),
             QStringLiteral("--binary"),
             firstSource,
             secondSource});
    ProcessTransferState state;
    connectProcessTransfer(&process, &receiver, &state, 2);

    process.start();
    if (!process.waitForStarted(3000)) {
        qCritical() << "cannot start sb" << process.errorString();
        return false;
    }
    receiver.receive(XyModemTransfer::Protocol::Ymodem,
                     destinationDirectory.path());
    if (!waitForProcessTransfer(
                &state,
                &process,
                QStringLiteral("sb interoperability failed"))) {
        return false;
    }
    if (readFile(destinationDirectory.filePath(
                QStringLiteral("from-sb-one.bin"))) != firstPayload ||
        readFile(destinationDirectory.filePath(
                QStringLiteral("from-sb-two.bin"))) != secondPayload) {
        qCritical() << "sb download contents mismatch";
        return false;
    }
    return true;
}

bool testLrzszYmodemUpload(const QString &rbExecutable) {
    QTemporaryDir sourceDirectory;
    QTemporaryDir destinationDirectory;
    if (!sourceDirectory.isValid() || !destinationDirectory.isValid()) {
        return false;
    }

    const QByteArray firstPayload = testPayload(17003);
    const QByteArray secondPayload = testPayload(513);
    const QString firstSource =
            sourceDirectory.filePath(QStringLiteral("to-rb-one.bin"));
    const QString secondSource =
            sourceDirectory.filePath(QStringLiteral("to-rb-two.bin"));
    if (!writeFile(firstSource, firstPayload) || !writeFile(secondSource, secondPayload)) {
        return false;
    }

    XyModemTransfer sender;
    QProcess process;
    process.setProgram(rbExecutable);
    process.setArguments(
            {QStringLiteral("--ymodem"),
             QStringLiteral("--binary")});
    process.setWorkingDirectory(destinationDirectory.path());
    ProcessTransferState state;
    connectProcessTransfer(&process, &sender, &state, 2);

    process.start();
    if (!process.waitForStarted(3000)) {
        qCritical() << "cannot start rb" << process.errorString();
        return false;
    }
    sender.send(XyModemTransfer::Protocol::Ymodem,
                {firstSource, secondSource});
    if (!waitForProcessTransfer(
                &state,
                &process,
                QStringLiteral("rb interoperability failed"))) {
        return false;
    }
    if (readFile(destinationDirectory.filePath(
                QStringLiteral("to-rb-one.bin"))) != firstPayload ||
        readFile(destinationDirectory.filePath(
                QStringLiteral("to-rb-two.bin"))) != secondPayload) {
        qCritical() << "rb upload contents mismatch";
        return false;
    }
    return true;
}

}// namespace

int main(int argc, char *argv[]) {
    QCoreApplication application(argc, argv);

    if (!testProtocolCodec() || !testXmodemChecksumUpload() || !testXmodemReceivers() || !testYmodemInMemoryTransfer() || !testCanceledDownloadCleanup()) {
        return 1;
    }

    const QString sxExecutable =
            QStandardPaths::findExecutable(QStringLiteral("sx"));
    const QString rxExecutable =
            QStandardPaths::findExecutable(QStringLiteral("rx"));
    const QString sbExecutable =
            QStandardPaths::findExecutable(QStringLiteral("sb"));
    const QString rbExecutable =
            QStandardPaths::findExecutable(QStringLiteral("rb"));
    if (sxExecutable.isEmpty() || rxExecutable.isEmpty() || sbExecutable.isEmpty() || rbExecutable.isEmpty()) {
        qInfo() << "lrzsz is not installed; "
                   "skipping X/YMODEM interoperability checks";
        return 0;
    }
    if (!testLrzszXmodemDownload(sxExecutable) || !testLrzszXmodemUpload(rxExecutable, false) || !testLrzszXmodemUpload(rxExecutable, true) || !testLrzszYmodemDownload(sbExecutable) || !testLrzszYmodemUpload(rbExecutable)) {
        return 1;
    }
    return 0;
}
