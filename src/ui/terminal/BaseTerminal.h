#ifndef QSHELL_BASE_TERMINAL_H
#define QSHELL_BASE_TERMINAL_H

#include "qtermwidget.h"
#include "core/datatype.h"
#include "core/zmodem/ZmodemTransfer.h"
#include "core/xymodem/XyModemCommandDetector.h"
#include "core/xymodem/XyModemTransfer.h"
#include <QElapsedTimer>
#include <QFile>
#include <QMenu>
#include <QStringList>
#include <QColorDialog>
#include <QTimer>

class QFileDialog;
class IPtyProcess;
class QProgressDialog;

class BaseTerminal : public QTermWidget {

    Q_OBJECT

public:
    explicit BaseTerminal(QWidget *parent);
    ~BaseTerminal() override;

    void startLocalShell();
    virtual void connect() = 0;
    virtual void disconnect() = 0;
    bool isConnect() const;

    // 日志相关方法
    bool isLogging() const;
    QString logFilePath() const;
    QString getSessionName() const;
    void populateFileTransferMenu(QMenu *menu);
    bool prepareZmodemUpload(const QStringList &filePaths);
    bool prepareZmodemDownload(const QString &directoryPath);

    signals:
        void onSessionError(BaseTerminal *terminal);
    void loggingStateChanged(bool isLogging);

protected:
    void onDisplayOutput(const QString &line);
    void onCopyAvailable(bool copyAvailable);
    void receiveBackendData(const QByteArray &data);
    virtual void writeToBackend(const QByteArray &data) = 0;

    // 右键菜单事件
    void contextMenuEvent(QContextMenuEvent *event) override;

private slots:
    void onToggleLogging(bool includeBufferedLogs);

private:
    void startLogging(const QString &filePath, bool includeBufferedLogs);
    void stopLogging();
    void writeToLog(const QString &line);
    void onZmodemDetected(ZmodemTransfer::Direction direction);
    void onZmodemFileStarted(ZmodemTransfer::Direction direction,
                             const QString &fileName,
                             qint64 size,
                             int fileNumber,
                             int fileCount);
    void onZmodemFileProgress(ZmodemTransfer::Direction direction,
                              const QString &fileName,
                              qint64 transferred,
                              qint64 size);
    void closeZmodemProgress();
    void startXyModemSend(XyModemTransfer::Protocol protocol);
    void startXyModemReceive(XyModemTransfer::Protocol protocol);
    QStringList selectXyModemSendFiles(
            XyModemTransfer::Protocol protocol);
    QString selectXyModemReceiveDestination(
            XyModemTransfer::Protocol protocol);
    bool executeXyModemFileDialog(QFileDialog *dialog);
    void beginPendingXyModemCommand(XyModemCommand command);
    void processPendingXyModemData(const QByteArray &data);
    void startPendingXyModemTransfer();
    void clearPendingXyModemCommand();
    void displayBackendData(const QByteArray &data);
    void onXyModemFileStarted(XyModemTransfer::Protocol protocol,
                              XyModemTransfer::Direction direction,
                              const QString &fileName,
                              qint64 size,
                              int fileNumber,
                              int fileCount);
    void onXyModemFileProgress(XyModemTransfer::Protocol protocol,
                               XyModemTransfer::Direction direction,
                               const QString &fileName,
                               qint64 transferred,
                               qint64 size);
    void closeXyModemProgress();

    // 高亮菜单相关方法
    void buildHighlightMenu(QMenu *parentMenu);
    static QColor generateRandomColor();

protected:
    SessionData sessionData_;
    QFont *font_ = nullptr;
    bool connect_ = false;
    IPtyProcess *localShell_ = nullptr;

private:
    // 日志相关成员
    bool logging_ = false;
    QFile *logFile_ = nullptr;
    QString logFilePath_;
    XyModemCommandDetector xyModemCommandDetector_;
    XyModemCommand pendingXyModemCommand_ =
            XyModemCommand::None;
    QTimer pendingXyModemTimer_;
    QByteArray pendingXyModemOutput_;
    QByteArray pendingXyModemProtocolData_;
    bool pendingXyModemDialogScheduled_ = false;
    QFileDialog *xyModemFileDialog_ = nullptr;
    XyModemTransfer *xyModemTransfer_ = nullptr;
    QProgressDialog *xyModemProgress_ = nullptr;
    QString xyModemDirectory_;
    ZmodemTransfer *zmodemTransfer_ = nullptr;
    QProgressDialog *zmodemProgress_ = nullptr;
    QString zmodemProgressLabel_;
    QElapsedTimer zmodemRateTimer_;
    qint64 zmodemRateTransferred_ = 0;
    QString zmodemDirectory_;
    QStringList pendingZmodemUploadPaths_;
    QString pendingZmodemDownloadDirectory_;
};

#endif//QSHELL_BASE_TERMINAL_H
