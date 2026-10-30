// Exercise the shared event pump with deterministic SSH backpressure. The
// application code is included below with only its transport calls substituted;
// no server, saved credentials or timing-sensitive network conditions are needed.
#include "ui/terminal/BaseTerminal.h"
#include "ui/terminal/SshSocket.h"
#ifdef Q_OS_WIN
#include "ui/terminal/WindowsX11Server.h"
#else
#include "ui/terminal/UnixX11Display.h"
#endif
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QSocketNotifier>
#include <QTimer>
#include <cstdlib>
#include <cstring>
#include <libssh2.h>
#include <vector>

#define private public
#include "ui/terminal/SSHTerminal.h"
#undef private

namespace {
void require(bool condition, const char *message) {
    if (!condition) {
        qCritical() << message;
        std::exit(1);
    }
}

struct TestChannel {
    QByteArray incoming;
    QByteArray outgoing;
    int blockedDirections = 0;
    bool zeroRead = false;
    bool zeroWrite = false;
    bool eof = false;
    bool freed = false;
    const char *retryBuffer = nullptr;
    size_t retrySize = 0;
    unsigned long window = 1024 * 1024;
    TestChannel *receiveDuringWrite = nullptr;
    TestChannel *creditDuringRead = nullptr;
    LIBSSH2_CHANNEL *handle() { return reinterpret_cast<LIBSSH2_CHANNEL *>(this); }
};

int hostKeyType = LIBSSH2_HOSTKEY_TYPE_ED25519;
QByteArray hostKey("saved-key");
const char *testHostKey(LIBSSH2_SESSION *, size_t *length, int *type) {
    *length = size_t(hostKey.size());
    *type = hostKeyType;
    return hostKey.constData();
}
const char *testHostHash(LIBSSH2_SESSION *, int) { return nullptr; }
int testKnownHostsRead(LIBSSH2_KNOWNHOSTS *hosts, const char *, int) {
    return libssh2_knownhost_addc(hosts, "test-host", nullptr, "saved-key", 9,
                                 nullptr, 0, LIBSSH2_KNOWNHOST_TYPE_PLAIN |
                                 LIBSSH2_KNOWNHOST_KEYENC_RAW | LIBSSH2_KNOWNHOST_KEY_ED25519, nullptr);
}
int testKnownHostsWrite(LIBSSH2_KNOWNHOSTS *, const char *, int) { return 0; }

int directions = 0;
bool localBlocked = false;
QByteArray localOutput;
bool resizeBlocked = false;
QList<QSize> resizeRequests;

ssize_t testRead(LIBSSH2_CHANNEL *handle, int stream, char *buffer, size_t size) {
    auto &channel = *reinterpret_cast<TestChannel *>(handle);
    if (channel.creditDuringRead) {
        channel.creditDuringRead->window = 1024 * 1024;
        channel.creditDuringRead->blockedDirections = 0;
        channel.creditDuringRead = nullptr;
    }
    if (stream == 0 && !channel.incoming.isEmpty()) {
        const qsizetype count = qMin(qsizetype(size), channel.incoming.size());
        std::memcpy(buffer, channel.incoming.constData(), count);
        channel.incoming.remove(0, count);
        return count;
    }
    directions = LIBSSH2_SESSION_BLOCK_INBOUND;
    return channel.zeroRead || channel.eof ? 0 : LIBSSH2_ERROR_EAGAIN;
}

ssize_t testWrite(LIBSSH2_CHANNEL *handle, int, const char *buffer, size_t size) {
    auto &channel = *reinterpret_cast<TestChannel *>(handle);
    if (channel.zeroWrite) {
        directions = LIBSSH2_SESSION_BLOCK_INBOUND;
        return 0;
    }
    if (channel.retryBuffer) {
        require(channel.retryBuffer == buffer && channel.retrySize == size,
                "EAGAIN retry changed its buffer or length");
    }
    if (channel.blockedDirections) {
        directions = channel.blockedDirections;
        channel.retryBuffer = buffer;
        channel.retrySize = size;
        return LIBSSH2_ERROR_EAGAIN;
    }
    channel.retryBuffer = nullptr;
    channel.outgoing.append(buffer, qsizetype(size));
    if (channel.receiveDuringWrite) {
        channel.receiveDuringWrite->incoming.append("reply buffered by write");
        channel.receiveDuringWrite = nullptr;
    }
    return ssize_t(size);
}

int testPoll(LIBSSH2_CHANNEL *handle, int stream) {
    return stream == 0 && !reinterpret_cast<TestChannel *>(handle)->incoming.isEmpty();
}
int testDirections(LIBSSH2_SESSION *) { return directions; }
unsigned long testWindow(LIBSSH2_CHANNEL *handle, unsigned long *) {
    return reinterpret_cast<TestChannel *>(handle)->window;
}
int testEof(LIBSSH2_CHANNEL *handle) { return reinterpret_cast<TestChannel *>(handle)->eof; }
int testFree(LIBSSH2_CHANNEL *handle) {
    reinterpret_cast<TestChannel *>(handle)->freed = true;
    return 0;
}
int testClose(LIBSSH2_CHANNEL *) { return 0; }
int testResize(LIBSSH2_CHANNEL *, int columns, int rows, int, int) {
    resizeRequests.append(QSize(columns, rows));
    directions = LIBSSH2_SESSION_BLOCK_OUTBOUND;
    return resizeBlocked ? LIBSSH2_ERROR_EAGAIN : 0;
}
int testSend(libssh2_socket_t, const char *data, int size, int) {
    if (localBlocked) {
#ifdef Q_OS_WIN
        WSASetLastError(WSAEWOULDBLOCK);
#else
        errno = EWOULDBLOCK;
#endif
        return -1;
    }
    localOutput.append(data, size);
    return size;
}
}// namespace

#define libssh2_session_hostkey testHostKey
#define libssh2_hostkey_hash testHostHash
#define libssh2_knownhost_readfile testKnownHostsRead
#define libssh2_knownhost_writefile testKnownHostsWrite
#define libssh2_channel_read_ex testRead
#define libssh2_channel_write_ex testWrite
#define libssh2_poll_channel_read testPoll
#define libssh2_session_block_directions testDirections
#define libssh2_channel_window_write_ex testWindow
#define libssh2_channel_eof testEof
#define libssh2_channel_free testFree
#define libssh2_channel_send_eof testClose
#define libssh2_channel_close testClose
#define libssh2_channel_request_pty_size_ex testResize
#define send testSend
#include "../src/ui/terminal/SSHTerminal.cpp"
#undef send

int main(int argc, char *argv[]) {
    Q_INIT_RESOURCE(qtermwidget);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("qshell-x11-pump-test");
    QCoreApplication::setApplicationName("qshell-x11-pump-test");
    {
        SessionData data;
        data.sshConfig.host = "test-host";
        data.sshConfig.port = 22;
        SSHTerminal verifier(data);
        verifier.session_ = libssh2_session_init();
        require(verifier.session_ != nullptr, "cannot allocate verification session");
        QObject::connect(&verifier, &BaseTerminal::onSessionError, &verifier,
                         [&verifier](BaseTerminal *) { verifier.disconnect(); });
        // Close any error dialog automatically, just as the user clicks OK.
        QTimer closer;
        QObject::connect(&closer, &QTimer::timeout, [] {
            if (auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                dialog->accept();
        });
        closer.start(10);
        require(verifier.verifyHostKey(), "matching stored key was rejected");
        hostKeyType = LIBSSH2_HOSTKEY_TYPE_ECDSA_256;
        hostKey = "ecdsa-key";
        require(verifier.verifyHostKey(), "different key algorithm was reported as a changed key");
        hostKeyType = LIBSSH2_HOSTKEY_TYPE_ED25519;
        hostKey = "changed-key";
        require(!verifier.verifyHostKey(), "changed key of the same algorithm was accepted");
        require(verifier.session_ == nullptr, "error handler did not disconnect synchronously");
    }
    TestChannel shell;
    TestChannel x11;
    SSHTerminal terminal(SessionData{});
    terminal.running_ = true;
    terminal.channel_ = shell.handle();
    // Only the substituted block-directions function uses this sentinel.
    terminal.session_ = reinterpret_cast<LIBSSH2_SESSION *>(1);
    const libssh2_socket_t socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    require(socket != LIBSSH2_INVALID_SOCKET, "cannot allocate notifier socket");
    terminal.writeNotifier_ = new QSocketNotifier(qintptr(socket), QSocketNotifier::Write, &terminal);
    terminal.writeNotifier_->setEnabled(false);
    auto *forward = new SSHTerminal::X11Forward;
    forward->chan = x11.handle();
    forward->notifier = new QSocketNotifier(qintptr(socket), QSocketNotifier::Read, &terminal);
    forward->writable = new QSocketNotifier(qintptr(socket), QSocketNotifier::Write, &terminal);
    forward->writable->setEnabled(false);
    terminal.x11Chans_.push_back(forward);

    // A write-only wakeup must flush X11 even when the shell has no output.
    x11.blockedDirections = LIBSSH2_SESSION_BLOCK_OUTBOUND;
    forward->toRemote = "first";
    terminal.flushX11ToRemote(forward);
    require(terminal.writeNotifier_->isEnabled(), "X11 did not request a write wakeup");
    forward->toRemote.append("second");
    x11.blockedDirections = 0;
    terminal.onSocketWritable();
    require(x11.outgoing == "firstsecond", "write wakeup did not drain X11 in order");
    require(!terminal.writeNotifier_->isEnabled(), "idle socket still requests write wakeups");

    // A remote window shortage must wait for input, without a writable busy loop.
    x11.blockedDirections = LIBSSH2_SESSION_BLOCK_INBOUND;
    forward->toRemote = "window";
    terminal.onSocketReadyRead();
    require(!terminal.writeNotifier_->isEnabled(), "inbound wait spins on writable socket");
    x11.blockedDirections = 0;
    terminal.onSocketReadyRead();
    require(x11.outgoing.endsWith("window"), "window-adjust read did not resume X11");

    x11.zeroWrite = true;
    forward->toRemote = "zero-window";
    terminal.onSocketReadyRead();
    require(!forward->closed && !terminal.writeNotifier_->isEnabled(),
            "zero write closed the channel or started a writable busy loop");
    x11.zeroWrite = false;
    terminal.onSocketReadyRead();
    require(x11.outgoing.endsWith("zero-window"), "zero write lost pending bytes");

    // The shell must also preserve retry arguments when more input arrives.
    shell.blockedDirections = LIBSSH2_SESSION_BLOCK_OUTBOUND;
    terminal.pendingWrite_ = "shell-first";
    terminal.flushPendingWrite();
    terminal.pendingWrite_.append("shell-second");
    shell.blockedDirections = 0;
    terminal.onSocketWritable();
    require(shell.outgoing == "shell-firstshell-second", "shell retry lost or reordered data");

    // A resize also owns libssh2's pending packet until the same request resumes.
    resizeBlocked = true;
    terminal.resizePty(80, 24);
    terminal.resizePty(100, 30);
    resizeBlocked = false;
    terminal.onSocketWritable();
    require(resizeRequests == QList<QSize>{QSize(80, 24), QSize(80, 24), QSize(80, 24), QSize(100, 30)},
            "PTY resize retry changed arguments or lost the latest size");

    // A read on another channel can consume a window adjustment without
    // payload or another socket event. Pending input still needs a retry.
    terminal.ioPumpScheduled_ = false;
    shell.window = 0;
    shell.blockedDirections = LIBSSH2_SESSION_BLOCK_INBOUND;
    terminal.pendingWrite_ = "credit";
    x11.creditDuringRead = &shell;
    terminal.onSocketReadyRead();
    require(terminal.ioPumpScheduled_, "cross-channel window adjustment stranded input");
    terminal.onSocketReadyRead();
    require(shell.outgoing.endsWith("credit"), "restored window credit did not resume input");

    // A write can read a reply into another channel with no new socket edge.
    x11.receiveDuringWrite = &x11;
    forward->toRemote = "request";
    terminal.onSocketWritable();
    require(terminal.ioPumpScheduled_, "buffered reply was stranded waiting for socket input");
    terminal.onSocketReadyRead();
    require(localOutput == "reply buffered by write", "buffered reply was not forwarded");

    // Zero without EOF is not a close. Idle processing must not schedule polling.
    terminal.ioPumpScheduled_ = false;
    x11.zeroRead = true;
    terminal.onSocketReadyRead();
    require(!forward->closed && !x11.freed, "zero read closed a live X11 channel");
    require(!terminal.ioPumpScheduled_, "idle X11 channel schedules a busy loop");
    x11.zeroRead = false;

    // Bulk drawing must yield to Qt and preserve the complete ordered stream.
    localOutput.clear();
    const QByteArray drawing(1024 * 1024, 'x');
    x11.incoming = drawing;
    terminal.pumpRemoteX11();
    require(!x11.incoming.isEmpty(), "one pump monopolized an entire drawing burst");
    require(terminal.ioPumpScheduled_, "bounded pump did not schedule continuation");
    while (!x11.incoming.isEmpty()) terminal.pumpRemoteX11();
    require(localOutput == drawing, "bounded reads lost drawing bytes");

    // A blocked local server must bound its queue and retain final bytes at EOF.
    localOutput.clear();
    localBlocked = true;
    x11.incoming = drawing;
    terminal.pumpRemoteX11();
    require(forward->toLocal.size() <= ioBatchBytes, "local backpressure queue is unbounded");
    require(forward->writable->isEnabled(), "local backpressure has no write wakeup");
    localBlocked = false;
    terminal.flushX11ToLocal(forward);
    while (!x11.incoming.isEmpty()) terminal.pumpRemoteX11();
    require(localOutput == drawing, "local backpressure lost drawing bytes");
    localOutput.clear();
    localBlocked = true;
    x11.incoming = "final drawing";
    x11.eof = true;
    terminal.pumpRemoteX11();
    require(!forward->closed && !x11.freed, "EOF discarded pending local bytes");
    localBlocked = false;
    terminal.flushX11ToLocal(forward);
    require(localOutput == "final drawing" && x11.freed, "EOF did not flush before close");

    terminal.session_ = nullptr;
    terminal.disconnect();
    SshSocket::close(socket);
    qInfo() << "Shared SSH/X11 retries, buffered reads, fairness, backpressure and EOF passed";
    return 0;
}
