#ifndef QSHELL_SSH_TERMINAL_H
#define QSHELL_SSH_TERMINAL_H

#include "BaseTerminal.h"
#include "core/datatype.h"
#include <QDir>
#include <QFile>
#include <QSocketNotifier>

#include "SshSocket.h"
#include <vector>
#ifndef Q_OS_WIN
#include "UnixX11Display.h"
#endif

class SSHTerminal : public BaseTerminal {
    Q_OBJECT
public:
    explicit SSHTerminal(const SessionData &session, QWidget *parent = nullptr);
    ~SSHTerminal() override;
    void connect() override;
    void disconnect() override;

protected:
    void writeToBackend(const QByteArray &data) override;

private:
    bool createSocket();
    bool connectToHost();
    bool initSession();
    bool verifyHostKey();
    bool authenticate();
    bool openChannel();

    static QString getKnownHostsPath();
    void cleanup();
    void resizePty(int cols, int rows);
    void syncPtySize();
    void flushPendingResize();
    void readAvailableData();

private slots:
    void onSocketReadyRead();
    void onSocketWritable();

private:
    void flushPendingWrite();
    void scheduleIoPump();
    void scheduleBufferedReads();
    void watchSessionWrite();

private:
    struct X11Forward {
        LIBSSH2_CHANNEL *chan = nullptr;
        libssh2_socket_t xsock = LIBSSH2_INVALID_SOCKET;
        QSocketNotifier *notifier = nullptr;
        QSocketNotifier *writable = nullptr;
        QByteArray toLocal;
        QByteArray toRemote;
        // Keep the arguments of an EAGAIN write stable while new data queues.
        QByteArray remoteWrite;
        bool waitingForWindow = false;
        bool remoteEof = false;
        bool closed = false;
    };

    static void x11Callback(LIBSSH2_SESSION *session,
                            LIBSSH2_CHANNEL *channel,
                            const char *shost, int sport,
                            void **abstract);
    void queueNewX11Channel(LIBSSH2_CHANNEL *chan);
    void drainPendingX11Channels();
    void handleNewX11Channel(LIBSSH2_CHANNEL *chan);
    void pumpRemoteX11();
    static void flushX11ToLocal(X11Forward *xf);
    void flushX11ToRemote(X11Forward *xf);
    static void closeX11Forward(X11Forward *xf);
    void purgeClosedX11Forwards();

private:
    libssh2_socket_t sock_ = LIBSSH2_INVALID_SOCKET;
    LIBSSH2_SESSION *session_ = nullptr;
    LIBSSH2_CHANNEL *channel_ = nullptr;
    bool running_ = false;
    bool x11ForwardingEnabled_ = false;
    QSocketNotifier *readNotifier_ = nullptr;
    QSocketNotifier *writeNotifier_ = nullptr;
    QByteArray pendingWrite_;
    QByteArray activeWrite_;
    bool waitingForWriteWindow_ = false;
    bool ioPumpScheduled_ = false;
    QSize pendingPtySize_;
    QSize activePtySize_;

    std::vector<LIBSSH2_CHANNEL *> pendingX11Chans_;
    std::vector<X11Forward *> x11Chans_;
#ifndef Q_OS_WIN
    UnixX11Display x11Display_;
#endif
};

#endif // QSHELL_SSH_TERMINAL_H
