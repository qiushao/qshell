#ifndef QSHELL_ZMODEM_TRANSFER_H
#define QSHELL_ZMODEM_TRANSFER_H

#include "ZmodemProtocol.h"

#include <QFile>
#include <QObject>
#include <QSaveFile>
#include <QStringList>
#include <QTimer>

#include <memory>

class ZmodemTransfer : public QObject {
    Q_OBJECT

public:
    enum class Direction {
        Download,
        Upload
    };
    Q_ENUM(Direction)

    explicit ZmodemTransfer(QObject *parent = nullptr);

    QByteArray consume(const QByteArray &data);
    bool isActive() const;
    Direction direction() const;

    void acceptDownload(const QString &directory);
    void acceptUpload(const QStringList &filePaths);
    void reject();
    void cancel();

signals:
    void detected(ZmodemTransfer::Direction direction);
    void outboundData(const QByteArray &data);
    void fileStarted(ZmodemTransfer::Direction direction,
                     const QString &fileName,
                     qint64 size,
                     int fileNumber,
                     int fileCount);
    void fileProgress(ZmodemTransfer::Direction direction,
                      const QString &fileName,
                      qint64 transferred,
                      qint64 size);
    void fileFinished(ZmodemTransfer::Direction direction,
                      const QString &filePath);
    void fileSkipped(ZmodemTransfer::Direction direction,
                     const QString &fileName);
    void transferFinished(ZmodemTransfer::Direction direction,
                          int fileCount);
    void transferCanceled(ZmodemTransfer::Direction direction);
    void transferFailed(ZmodemTransfer::Direction direction,
                        const QString &message);
    void terminalDataReady(const QByteArray &data);

private slots:
    void flushPendingTerminalData();
    void onTimeout();
    void pumpUpload();

private:
    enum class State {
        Idle,
        PendingDownload,
        PendingUpload,
        DownloadWaitFile,
        DownloadWaitSinitData,
        DownloadWaitFileInfo,
        DownloadWaitDataHeader,
        DownloadData,
        DownloadWaitEof,
        DownloadWaitOverAndOut,
        UploadWaitFileResponse,
        UploadData,
        UploadWaitDataAck,
        UploadWaitFileDone,
        UploadWaitFinish
    };

    bool handleHeader(const Zmodem::Header &header);
    void handleDataSubpacket(const Zmodem::ParseItem &item);
    void handleParseError(const QString &message);
    void handlePlainText(const QByteArray &data, QByteArray *terminalOutput);
    void handleRemoteCancel();

    void sendImmediate(const QByteArray &data);
    void sendRetryable(const QByteArray &data);
    void waitWithRetry(const QByteArray &data);
    void resetRetry();

    void sendReceiveInit();
    void handleFileInformation(const QByteArray &data);
    static QString safeDownloadName(const QByteArray &remoteName);
    QString uniqueDownloadPath(const QString &fileName) const;
    void finishDownloadedFile();
    void closeDownloadFile();

    void offerNextUpload();
    QByteArray uploadFileInformation() const;
    void startUploadAt(quint32 position);
    void finishUploadedFile();
    void closeUploadFile();

    void completeTransfer();
    void failTransfer(const QString &message, bool notifyPeer);
    void cancelTransfer(bool notifyPeer);
    void resetTransfer(bool resetParser = true);

    Zmodem::Parser parser_;
    State state_ = State::Idle;
    Direction direction_ = Direction::Download;
    Zmodem::Header initialHeader_;
    QTimer timeout_;
    QTimer detectionFlushTimer_;
    QByteArray retryPacket_;
    int retryCount_ = 0;
    int protocolErrorCount_ = 0;

    QString downloadDirectory_;
    std::unique_ptr<QSaveFile> downloadFile_;
    QString downloadFileName_;
    QString downloadFilePath_;
    qint64 downloadOffset_ = 0;
    qint64 downloadSize_ = -1;
    qint64 downloadModificationTime_ = -1;
    int downloadedFileCount_ = 0;
    int expectedDownloadFileCount_ = 0;
    QByteArray closingBytes_;

    QStringList uploadPaths_;
    int uploadIndex_ = 0;
    QFile uploadFile_;
    QString uploadFileName_;
    qint64 uploadOffset_ = 0;
    qint64 uploadAcknowledgedOffset_ = 0;
    qint64 uploadSize_ = 0;
    quint64 uploadGeneration_ = 0;
    qint64 uploadRemainingBytes_ = 0;
    int uploadedFileCount_ = 0;
    int uploadChunkSize_ = 1024;
    int uploadPacketsPerWindow_ = 8;
    bool uploadUsesCrc32_ = false;
    bool uploadEscapesControl_ = false;
};

#endif// QSHELL_ZMODEM_TRANSFER_H
