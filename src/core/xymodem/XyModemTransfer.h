#ifndef QSHELL_XYMODEM_TRANSFER_H
#define QSHELL_XYMODEM_TRANSFER_H

#include "XyModemProtocol.h"

#include <QFile>
#include <QObject>
#include <QSaveFile>
#include <QStringList>
#include <QTimer>

#include <memory>

class XyModemTransfer : public QObject {
    Q_OBJECT

public:
    enum class Protocol {
        Xmodem,
        Ymodem
    };
    Q_ENUM(Protocol)

    enum class Direction {
        Download,
        Upload
    };
    Q_ENUM(Direction)

    explicit XyModemTransfer(QObject *parent = nullptr);

    QByteArray consume(const QByteArray &data);
    bool isActive() const;
    Protocol protocol() const;
    Direction direction() const;

    void send(Protocol protocol, const QStringList &filePaths);
    void receive(Protocol protocol, const QString &destination);
    void cancel();

signals:
    void outboundData(const QByteArray &data);
    void fileStarted(XyModemTransfer::Protocol protocol,
                     XyModemTransfer::Direction direction,
                     const QString &fileName,
                     qint64 size,
                     int fileNumber,
                     int fileCount);
    void fileProgress(XyModemTransfer::Protocol protocol,
                      XyModemTransfer::Direction direction,
                      const QString &fileName,
                      qint64 transferred,
                      qint64 size);
    void fileFinished(XyModemTransfer::Protocol protocol,
                      XyModemTransfer::Direction direction,
                      const QString &filePath);
    void transferFinished(XyModemTransfer::Protocol protocol,
                          XyModemTransfer::Direction direction,
                          int fileCount);
    void transferCanceled(XyModemTransfer::Protocol protocol,
                          XyModemTransfer::Direction direction);
    void transferFailed(XyModemTransfer::Protocol protocol,
                        XyModemTransfer::Direction direction,
                        const QString &message);

private slots:
    void onTimeout();

private:
    enum class State {
        Idle,
        SendWaitStart,
        SendWaitMetadataAck,
        SendWaitMetadataRequest,
        SendWaitDataAck,
        SendWaitFirstEotResponse,
        SendWaitFinalEotAck,
        SendWaitNextFileRequest,
        SendWaitBatchEndAck,
        ReceiveWaitMetadata,
        ReceiveWaitData,
        ReceiveWaitSecondEot
    };

    void processSenderInput(QByteArray *terminalOutput);
    void handleSenderControl(char value);
    void processReceiverInput(QByteArray *terminalOutput);
    void handleReceivedPacket(const XyModem::Packet &packet);
    void handleReceiveEot();

    static bool validateUploadPaths(Protocol protocol,
                                    const QStringList &filePaths,
                                    QString *errorMessage);
    QByteArray uploadMetadata(int index) const;
    void offerCurrentUpload();
    void sendNextUploadBlock();
    void finishUploadedFile();

    void handleDownloadMetadata(const QByteArray &data);
    bool writeReceivedData(const QByteArray &data);
    bool finishReceivedFile();
    static QString safeDownloadName(const QByteArray &remoteName);
    QString uniqueDownloadPath(const QString &fileName) const;
    void closeDownloadFile();

    void transmit(const QByteArray &data, State state);
    void respond(const QByteArray &data);
    bool retry(const QByteArray &data);
    void resetRetry();

    void completeTransfer();
    void failTransfer(const QString &message, bool notifyPeer);
    void handleRemoteCancel();
    void resetTransfer();

    State state_ = State::Idle;
    Protocol protocol_ = Protocol::Xmodem;
    Direction direction_ = Direction::Download;
    XyModem::CheckMode checkMode_ = XyModem::CheckMode::Crc16;
    QTimer timeout_;
    QByteArray inputBuffer_;
    QByteArray retryPacket_;
    int retryCount_ = 0;
    int protocolErrorCount_ = 0;

    QStringList uploadPaths_;
    int uploadIndex_ = 0;
    QFile uploadFile_;
    QString uploadFileName_;
    qint64 uploadSize_ = 0;
    qint64 uploadOffset_ = 0;
    qint64 uploadPacketBytes_ = 0;
    quint8 uploadBlockNumber_ = 1;
    int uploadedFileCount_ = 0;

    QString downloadDestination_;
    std::unique_ptr<QSaveFile> downloadFile_;
    QString downloadFileName_;
    QString downloadFilePath_;
    qint64 downloadSize_ = -1;
    qint64 downloadOffset_ = 0;
    qint64 downloadModificationTime_ = -1;
    QByteArray pendingDownloadData_;
    quint8 expectedDownloadBlock_ = 1;
    int downloadedFileCount_ = 0;
    int expectedDownloadFileCount_ = 0;
    int initialCrcRequestCount_ = 0;
};

#endif// QSHELL_XYMODEM_TRANSFER_H
