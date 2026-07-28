#ifndef QSHELL_SSH_TERMINAL_H
#define QSHELL_SSH_TERMINAL_H

#include "BaseTerminal.h"
#include "core/datatype.h"
#include <QDir>
#include <QFile>
#include <QSocketNotifier>

#if defined(Q_CC_MSVC)
#include <vector>
#include <winsock2.h>
#include <libssh2.h>
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

#if defined(Q_CC_MSVC)
private:
    bool createSocket();
    bool connectToHost();
    bool initSession();
    bool verifyHostKey();
    bool authenticate();
    bool openChannel();

    QString getKnownHostsPath();
    void cleanup();
    void resizePty(int cols, int rows);
    void syncPtySize();
    void readAvailableData();

private slots:
    void onSocketReadyRead();

private:
    struct X11Forward {
        LIBSSH2_CHANNEL *chan = nullptr;
        SOCKET xsock = INVALID_SOCKET;
        QSocketNotifier *notifier = nullptr;
        QSocketNotifier *writable = nullptr;
        QByteArray toLocal;
        QByteArray toRemote;
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
    void flushX11ToLocal(X11Forward *xf);
    void flushX11ToRemote(X11Forward *xf);
    void closeX11Forward(X11Forward *xf);
    void purgeClosedX11Forwards();

private:
    SOCKET sock_ = INVALID_SOCKET;
    LIBSSH2_SESSION *session_ = nullptr;
    LIBSSH2_CHANNEL *channel_ = nullptr;
    bool running_ = false;
    bool x11ForwardingEnabled_ = false;
    QSocketNotifier *readNotifier_ = nullptr;

    std::vector<LIBSSH2_CHANNEL *> pendingX11Chans_;
    std::vector<X11Forward *> x11Chans_;
#endif
};

#endif // QSHELL_SSH_TERMINAL_H
