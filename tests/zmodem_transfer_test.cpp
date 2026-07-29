#include "core/zmodem/ZmodemProtocol.h"
#include "core/zmodem/ZmodemTransfer.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
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

QByteArray testPayload() {
    QByteArray data;
    data.reserve(32768);
    for (int repeat = 0; repeat < 128; ++repeat) {
        for (int value = 0; value < 256; ++value) {
            data.append(static_cast<char>(value));
        }
    }
    return data;
}

bool runUntil(const std::function<bool()> &done,
              int timeoutMilliseconds) {
    if (done()) {
        return true;
    }

    QEventLoop loop;
    QTimer completionTimer;
    completionTimer.setInterval(10);
    QObject::connect(&completionTimer, &QTimer::timeout,
                     &loop, [&loop, &done]() {
                         if (done()) {
                             loop.quit();
                         }
                     });
    completionTimer.start();
    QTimer::singleShot(timeoutMilliseconds, &loop, &QEventLoop::quit);
    loop.exec();
    return done();
}

void deliverFragmented(ZmodemTransfer *target,
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
        chunkSize = chunkSize == 17 ? 1 : chunkSize + 1;
    }
}

bool testProtocolCodec() {
    const QByteArray expectedZrinit(
            "**\x18"
            "B0100000023be50\r\x8a\x11",
            21);
    const std::array<quint8, 4> flags{
            0x00U, 0x00U, 0x00U, 0x23U};
    const QByteArray encodedZrinit = Zmodem::encodeHexHeader(
            Zmodem::FrameType::Zrinit, flags);
    if (encodedZrinit != expectedZrinit) {
        qCritical() << "ZRINIT vector mismatch"
                    << encodedZrinit.toHex()
                    << expectedZrinit.toHex();
        return false;
    }

    for (const bool crc32: {false, true}) {
        Zmodem::Parser parser;
        QByteArray stream = Zmodem::encodeBinaryHeader(
                Zmodem::FrameType::Zdata,
                0x12345678U,
                crc32,
                true);
        const QByteArray payload = testPayload().left(8192);
        stream.append(Zmodem::encodeDataSubpacket(
                payload,
                Zmodem::FrameEnd::EndAck,
                crc32,
                true));

        bool gotHeader = false;
        bool gotData = false;
        for (const char byte: stream) {
            parser.addData(QByteArray(1, byte));
            Zmodem::ParseItem item;
            while (parser.next(&item)) {
                if (item.kind == Zmodem::ParseItem::Kind::Header) {
                    if (gotHeader || item.header.type != Zmodem::FrameType::Zdata || item.header.position() != 0x12345678U || item.header.usesCrc32() != crc32) {
                        qCritical() << "invalid parsed binary header";
                        return false;
                    }
                    gotHeader = true;
                    parser.expectDataSubpacket();
                } else if (item.kind == Zmodem::ParseItem::Kind::Data) {
                    if (!gotHeader || gotData || item.data != payload || item.frameEnd != Zmodem::FrameEnd::EndAck) {
                        qCritical() << "invalid parsed data packet";
                        return false;
                    }
                    gotData = true;
                } else if (item.kind != Zmodem::ParseItem::Kind::PlainText) {
                    qCritical() << "unexpected parser item";
                    return false;
                }
            }
        }
        if (!gotHeader || !gotData) {
            qCritical() << "fragmented packet was not fully parsed";
            return false;
        }
    }

    ZmodemTransfer transfer;
    QByteArray delayedTerminalData;
    QObject::connect(&transfer,
                     &ZmodemTransfer::terminalDataReady,
                     &transfer,
                     [&delayedTerminalData](
                             const QByteArray &data) {
                         delayedTerminalData.append(data);
                     });
    if (!transfer.consume(QByteArrayLiteral("*")).isEmpty() || !runUntil(
                                                                       [&delayedTerminalData]() {
                                                                           return delayedTerminalData == QByteArrayLiteral("*");
                                                                       },
                                                                       1000)) {
        qCritical() << "pending terminal prefix was not flushed";
        return false;
    }

    ZmodemTransfer canceledTransfer;
    bool remotelyCanceled = false;
    QObject::connect(
            &canceledTransfer,
            &ZmodemTransfer::transferFailed,
            &canceledTransfer,
            [&remotelyCanceled](ZmodemTransfer::Direction,
                                const QString &) {
                remotelyCanceled = true;
            });
    canceledTransfer.consume(Zmodem::encodeHexHeader(
            Zmodem::FrameType::Zrqinit));
    QByteArray cancelOutput;
    const QByteArray cancelSequence = Zmodem::abortSequence();
    for (const char byte: cancelSequence) {
        cancelOutput.append(
                canceledTransfer.consume(QByteArray(1, byte)));
    }
    cancelOutput.append(
            canceledTransfer.consume(QByteArrayLiteral("prompt")));
    if (!remotelyCanceled || cancelOutput != QByteArrayLiteral("prompt")) {
        qCritical() << "fragmented remote cancellation failed"
                    << cancelOutput.toHex();
        return false;
    }

    return true;
}

bool testQueuedConsumptionYieldsToEventLoop() {
    ZmodemTransfer transfer;
    const QByteArray input(512 * 1024, 'x');
    QByteArray output;
    int outputBatches = 0;
    bool eventHandled = false;

    QObject::connect(
            &transfer,
            &ZmodemTransfer::terminalDataReady,
            &transfer,
            [&output, &outputBatches](const QByteArray &data) {
                output.append(data);
                ++outputBatches;
            });

    transfer.enqueueData(input);
    if (!output.isEmpty()) {
        qCritical() << "queued input was processed synchronously";
        return false;
    }
    QTimer::singleShot(
            0,
            &transfer,
            [&eventHandled]() {
                eventHandled = true;
            });

    if (!runUntil(
                [&]() {
                    return output.size() == input.size();
                },
                1000) ||
        output != input || outputBatches < 2 || !eventHandled) {
        qCritical() << "queued input did not yield to the event loop"
                    << output.size() << outputBatches << eventHandled;
        return false;
    }
    return true;
}

bool testInMemoryTransfer() {
    QTemporaryDir sourceDirectory;
    QTemporaryDir destinationDirectory;
    if (!sourceDirectory.isValid() || !destinationDirectory.isValid()) {
        qCritical() << "cannot create temporary directories";
        return false;
    }

    const QByteArray payload = testPayload();
    const QString binarySource =
            sourceDirectory.filePath(QStringLiteral("all-bytes.bin"));
    const QString emptySource =
            sourceDirectory.filePath(QStringLiteral("empty.bin"));
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

    ZmodemTransfer sender;
    ZmodemTransfer receiver;
    QString failure;
    bool senderFinished = false;
    bool receiverFinished = false;
    bool corruptedPacket = false;
    bool replacedAcknowledgment = false;
    QStringList downloadedPaths;

    QObject::connect(&sender, &ZmodemTransfer::outboundData,
                     &receiver,
                     [&receiver, &failure, &corruptedPacket](
                             const QByteArray &data) {
                         QByteArray forwardedData = data;
                         if (!corruptedPacket && forwardedData.size() > 100) {
                             forwardedData[20] =
                                     static_cast<char>(
                                             forwardedData.at(20) ^ 0x01);
                             corruptedPacket = true;
                         }
                         QTimer::singleShot(
                                 0,
                                 &receiver,
                                 [&receiver,
                                  &failure,
                                  forwardedData]() {
                                     deliverFragmented(
                                             &receiver,
                                             forwardedData,
                                             &failure);
                                 });
                     });
    QObject::connect(&receiver, &ZmodemTransfer::outboundData,
                     &sender,
                     [&sender,
                      &failure,
                      &replacedAcknowledgment](
                             const QByteArray &data) {
                         QByteArray forwardedData = data;
                         if (!replacedAcknowledgment && data.startsWith(
                                                                QByteArrayLiteral(
                                                                        "**\x18"
                                                                        "B0300040000"))) {
                             forwardedData =
                                     Zmodem::encodeHexHeader(
                                             Zmodem::FrameType::Znak);
                             replacedAcknowledgment = true;
                         }
                         QTimer::singleShot(
                                 0,
                                 &sender,
                                 [&sender,
                                  &failure,
                                  forwardedData]() {
                                     deliverFragmented(
                                             &sender,
                                             forwardedData,
                                             &failure);
                                 });
                     });
    QObject::connect(&receiver, &ZmodemTransfer::detected,
                     &receiver,
                     [&receiver, &destinationDirectory](
                             ZmodemTransfer::Direction direction) {
                         if (direction == ZmodemTransfer::Direction::Download) {
                             receiver.acceptDownload(
                                     destinationDirectory.path());
                         }
                     });
    QObject::connect(&sender, &ZmodemTransfer::detected,
                     &sender,
                     [&sender, binarySource, emptySource](
                             ZmodemTransfer::Direction direction) {
                         if (direction == ZmodemTransfer::Direction::Upload) {
                             sender.acceptUpload(
                                     {binarySource, emptySource});
                         }
                     });
    QObject::connect(&receiver, &ZmodemTransfer::fileFinished,
                     &receiver,
                     [&downloadedPaths](
                             ZmodemTransfer::Direction,
                             const QString &path) {
                         downloadedPaths.append(path);
                     });
    QObject::connect(&sender, &ZmodemTransfer::transferFinished,
                     &sender,
                     [&senderFinished, &failure](
                             ZmodemTransfer::Direction direction,
                             int count) {
                         senderFinished = true;
                         if (direction != ZmodemTransfer::Direction::Upload || count != 2) {
                             failure = QStringLiteral(
                                     "invalid sender completion");
                         }
                     });
    QObject::connect(&receiver,
                     &ZmodemTransfer::transferFinished,
                     &receiver,
                     [&receiverFinished, &failure](
                             ZmodemTransfer::Direction direction,
                             int count) {
                         receiverFinished = true;
                         if (direction != ZmodemTransfer::Direction::Download || count != 2) {
                             failure = QStringLiteral(
                                     "invalid receiver completion");
                         }
                     });
    const auto recordFailure =
            [&failure](ZmodemTransfer::Direction,
                       const QString &message) {
                if (failure.isEmpty()) {
                    failure = message;
                }
            };
    QObject::connect(&sender, &ZmodemTransfer::transferFailed,
                     &sender, recordFailure);
    QObject::connect(&receiver, &ZmodemTransfer::transferFailed,
                     &receiver, recordFailure);

    receiver.consume(Zmodem::encodeHexHeader(
            Zmodem::FrameType::Zrqinit));
    const bool completed = runUntil(
            [&]() {
                return !failure.isEmpty() || (senderFinished && receiverFinished);
            },
            10000);
    if (!completed || !failure.isEmpty() || !corruptedPacket || !replacedAcknowledgment) {
        qCritical() << "in-memory transfer failed"
                    << (failure.isEmpty()
                                ? QStringLiteral("timeout")
                                : failure);
        return false;
    }
    if (readFile(existingDestination) != existingData) {
        qCritical() << "existing destination was overwritten";
        return false;
    }
    if (downloadedPaths.size() != 2 || readFile(downloadedPaths.at(0)) != payload || !readFile(downloadedPaths.at(1)).isEmpty()) {
        qCritical() << "in-memory file contents mismatch"
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

    ZmodemTransfer receiver;
    bool canceled = false;
    QObject::connect(&receiver, &ZmodemTransfer::detected,
                     &receiver,
                     [&receiver, &destinationDirectory](
                             ZmodemTransfer::Direction direction) {
                         if (direction == ZmodemTransfer::Direction::Download) {
                             receiver.acceptDownload(
                                     destinationDirectory.path());
                         }
                     });
    QObject::connect(&receiver, &ZmodemTransfer::transferCanceled,
                     &receiver,
                     [&canceled](ZmodemTransfer::Direction) {
                         canceled = true;
                     });

    receiver.consume(Zmodem::encodeHexHeader(
            Zmodem::FrameType::Zrqinit));
    const std::array<quint8, 4> fileFlags{
            0x00U, 0x00U, 0x00U, Zmodem::binaryFile};
    QByteArray fileOffer = Zmodem::encodeBinaryHeader(
            Zmodem::FrameType::Zfile,
            fileFlags,
            true,
            false);
    QByteArray fileInformation("partial.bin");
    fileInformation.append('\0');
    fileInformation.append("100 0 100644 0 1 100");
    fileInformation.append('\0');
    fileOffer.append(Zmodem::encodeDataSubpacket(
            fileInformation,
            Zmodem::FrameEnd::EndAck,
            true,
            false));
    receiver.consume(fileOffer);

    QByteArray partialData = Zmodem::encodeBinaryHeader(
            Zmodem::FrameType::Zdata,
            0,
            true,
            false);
    partialData.append(Zmodem::encodeDataSubpacket(
            QByteArrayLiteral("incomplete"),
            Zmodem::FrameEnd::Continue,
            true,
            false));
    receiver.consume(partialData);
    receiver.cancel();

    const QStringList remainingFiles =
            QDir(destinationDirectory.path())
                    .entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
    if (!canceled || !remainingFiles.isEmpty()) {
        qCritical() << "canceled download left files behind"
                    << remainingFiles;
        return false;
    }
    return true;
}

bool testLrzszDownload(const QString &szExecutable,
                       bool forceCrc16) {
    QTemporaryDir sourceDirectory;
    QTemporaryDir destinationDirectory;
    if (!sourceDirectory.isValid() || !destinationDirectory.isValid()) {
        return false;
    }

    const QByteArray payload = testPayload();
    const QString sourcePath =
            sourceDirectory.filePath(QStringLiteral("from-sz.bin"));
    if (!writeFile(sourcePath, payload)) {
        return false;
    }

    ZmodemTransfer receiver;
    QProcess process;
    process.setProgram(szExecutable);
    QStringList arguments{QStringLiteral("--binary")};
    arguments.append(forceCrc16
                             ? QStringLiteral("--16-bit-crc")
                             : QStringLiteral("--escape"));
    arguments.append(sourcePath);
    process.setArguments(arguments);
    QString failure;
    QString downloadedPath;
    bool transferFinished = false;
    bool processFinished = false;

    QObject::connect(&process, &QProcess::readyReadStandardOutput,
                     &receiver, [&]() {
                         receiver.enqueueData(
                                 process.readAllStandardOutput());
                     });
    QObject::connect(&process, &QProcess::readyReadStandardError,
                     &process, [&]() {
                         process.readAllStandardError();
                     });
    QObject::connect(&receiver, &ZmodemTransfer::outboundData,
                     &process, [&process](const QByteArray &data) {
                         process.write(data);
                     });
    QObject::connect(&receiver, &ZmodemTransfer::detected,
                     &receiver, [&](ZmodemTransfer::Direction direction) {
                         if (direction != ZmodemTransfer::Direction::Download) {
                             failure = QStringLiteral(
                                     "sz detected with wrong direction");
                             return;
                         }
                         receiver.acceptDownload(
                                 destinationDirectory.path());
                     });
    QObject::connect(&receiver, &ZmodemTransfer::fileFinished,
                     &receiver,
                     [&](ZmodemTransfer::Direction,
                         const QString &path) {
                         downloadedPath = path;
                     });
    QObject::connect(&receiver,
                     &ZmodemTransfer::transferFinished,
                     &receiver,
                     [&](ZmodemTransfer::Direction, int count) {
                         transferFinished = count == 1;
                     });
    QObject::connect(&receiver, &ZmodemTransfer::transferFailed,
                     &receiver,
                     [&](ZmodemTransfer::Direction,
                         const QString &message) {
                         failure = message;
                     });
    QObject::connect(
            &process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            &process,
            [&](int exitCode, QProcess::ExitStatus exitStatus) {
                processFinished = true;
                if (exitStatus != QProcess::NormalExit || exitCode != 0) {
                    failure = QStringLiteral(
                                      "sz exited with code %1")
                                      .arg(exitCode);
                }
            });

    process.start();
    if (!process.waitForStarted(3000)) {
        qCritical() << "cannot start sz" << process.errorString();
        return false;
    }
    const bool completed = runUntil(
            [&]() {
                return !failure.isEmpty() || (transferFinished && processFinished);
            },
            15000);
    if (!completed || !failure.isEmpty()) {
        qCritical() << "sz interoperability failed"
                    << (failure.isEmpty()
                                ? QStringLiteral("timeout")
                                : failure);
        process.kill();
        process.waitForFinished();
        return false;
    }
    if (downloadedPath.isEmpty() || readFile(downloadedPath) != payload) {
        qCritical() << "sz download contents mismatch";
        return false;
    }
    return true;
}

bool testLrzszUpload(const QString &rzExecutable,
                     bool requestControlEscaping,
                     const QString &scriptExecutable = {}) {
    QTemporaryDir sourceDirectory;
    QTemporaryDir destinationDirectory;
    if (!sourceDirectory.isValid() || !destinationDirectory.isValid()) {
        return false;
    }

    const QByteArray payload = testPayload();
    const QString sourcePath =
            sourceDirectory.filePath(QStringLiteral("to-rz.bin"));
    if (!writeFile(sourcePath, payload)) {
        return false;
    }

    ZmodemTransfer sender;
    QProcess process;
    if (scriptExecutable.isEmpty()) {
        process.setProgram(rzExecutable);
        QStringList arguments{QStringLiteral("--binary")};
        if (requestControlEscaping) {
            arguments.append(QStringLiteral("--escape"));
        }
        process.setArguments(arguments);
    } else {
        process.setProgram(scriptExecutable);
        const QString command =
                QStringLiteral(
                        "expect -c 'set timeout -1; "
                        "fconfigure $user_spawn_id "
                        "-translation binary -encoding binary; "
                        "spawn %1 --binary; "
                        "fconfigure $spawn_id -translation binary "
                        "-encoding binary; interact'")
                        .arg(rzExecutable);
        process.setArguments(
                {QStringLiteral("-qefc"),
                 command,
                 QStringLiteral("/dev/null")});
    }
    process.setWorkingDirectory(destinationDirectory.path());
    QString failure;
    bool transferFinished = false;
    bool processFinished = false;

    QObject::connect(&process, &QProcess::readyReadStandardOutput,
                     &sender, [&]() {
                         sender.enqueueData(
                                 process.readAllStandardOutput());
                     });
    QObject::connect(&process, &QProcess::readyReadStandardError,
                     &process, [&]() {
                         process.readAllStandardError();
                     });
    QObject::connect(&sender, &ZmodemTransfer::outboundData,
                     &process, [&process](const QByteArray &data) {
                         process.write(data);
                     });
    QObject::connect(&sender, &ZmodemTransfer::detected,
                     &sender, [&](ZmodemTransfer::Direction direction) {
                         if (direction != ZmodemTransfer::Direction::Upload) {
                             failure = QStringLiteral(
                                     "rz detected with wrong direction");
                             return;
                         }
                         sender.acceptUpload({sourcePath});
                     });
    QObject::connect(&sender, &ZmodemTransfer::transferFinished,
                     &sender,
                     [&](ZmodemTransfer::Direction, int count) {
                         transferFinished = count == 1;
                     });
    QObject::connect(&sender, &ZmodemTransfer::transferFailed,
                     &sender,
                     [&](ZmodemTransfer::Direction,
                         const QString &message) {
                         failure = message;
                     });
    QObject::connect(
            &process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            &process,
            [&](int exitCode, QProcess::ExitStatus exitStatus) {
                processFinished = true;
                if (exitStatus != QProcess::NormalExit || exitCode != 0) {
                    failure = QStringLiteral(
                                      "rz exited with code %1")
                                      .arg(exitCode);
                }
            });

    process.start();
    if (!process.waitForStarted(3000)) {
        qCritical() << "cannot start rz" << process.errorString();
        return false;
    }
    const bool completed = runUntil(
            [&]() {
                return !failure.isEmpty() || (transferFinished && processFinished);
            },
            15000);
    if (!completed || !failure.isEmpty()) {
        qCritical() << "rz interoperability failed"
                    << (scriptExecutable.isEmpty()
                                ? QStringLiteral("pipe")
                                : QStringLiteral("nested PTY"))
                    << (failure.isEmpty()
                                ? QStringLiteral("timeout")
                                : failure);
        process.kill();
        process.waitForFinished();
        return false;
    }

    const QString receivedPath =
            destinationDirectory.filePath(QStringLiteral("to-rz.bin"));
    if (readFile(receivedPath) != payload) {
        qCritical() << "rz upload contents mismatch";
        return false;
    }
    return true;
}

}// namespace

int main(int argc, char *argv[]) {
    QCoreApplication application(argc, argv);

    if (!testProtocolCodec() ||
        !testQueuedConsumptionYieldsToEventLoop() ||
        !testInMemoryTransfer() ||
        !testCanceledDownloadCleanup()) {
        return 1;
    }

    const QString szExecutable =
            QStandardPaths::findExecutable(QStringLiteral("sz"));
    const QString rzExecutable =
            QStandardPaths::findExecutable(QStringLiteral("rz"));
    if (szExecutable.isEmpty() || rzExecutable.isEmpty()) {
        qInfo() << "lrzsz is not installed; skipping interoperability checks";
        return 0;
    }
    if (!testLrzszDownload(szExecutable, true) || !testLrzszDownload(szExecutable, false) || !testLrzszUpload(rzExecutable, true) || !testLrzszUpload(rzExecutable, false)) {
        return 1;
    }
#if defined(Q_OS_LINUX)
    const QString scriptExecutable =
            QStandardPaths::findExecutable(
                    QStringLiteral("script"));
    const QString expectExecutable =
            QStandardPaths::findExecutable(
                    QStringLiteral("expect"));
    if (!scriptExecutable.isEmpty() && !expectExecutable.isEmpty() && !testLrzszUpload(rzExecutable, false, scriptExecutable)) {
        return 1;
    }
#endif
    return 0;
}
