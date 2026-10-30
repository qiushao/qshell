#include "UnixX11Display.h"

#include <QLocalSocket>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTcpSocket>
#include <unistd.h>

bool UnixX11Display::prepare() {
    displayName_ = qEnvironmentVariable("DISPLAY");
    socketPath_.clear();
    host_.clear();
    cookie_.clear();
    lastError_.clear();
    port_ = 0;
    screen_ = 0;
    if (displayName_.isEmpty()) {
        lastError_ = QStringLiteral("DISPLAY is not set");
        return false;
    }

    static const QRegularExpression displayPattern(QStringLiteral("^(.*):([0-9]+)(?:\\.([0-9]+))?$"));
    const auto match = displayPattern.match(displayName_);
    bool displayOk = false;
    const int display = match.captured(2).toInt(&displayOk);
    bool screenOk = true;
    if (!match.captured(3).isEmpty()) screen_ = match.captured(3).toInt(&screenOk);
    if (!match.hasMatch() || !displayOk || !screenOk || display > 65535 - 6000) {
        lastError_ = QStringLiteral("Invalid DISPLAY: %1").arg(displayName_);
        return false;
    }

    host_ = match.captured(1);
    if (displayName_.startsWith('/')) {
        // XQuartz launchd displays are absolute Unix socket paths ending in :N.
        socketPath_ = displayName_.left(match.capturedEnd(2));
    } else if (host_.isEmpty() || (host_ == "unix" || host_ == "unix/") || host_.endsWith("/unix")) {
        socketPath_ = QStringLiteral("/tmp/.X11-unix/X%1").arg(display);
    } else {
        if (host_.startsWith("tcp/")) host_.remove(0, 4);
        if (host_.startsWith('[') && host_.endsWith(']')) host_ = host_.mid(1, host_.size() - 2);
        port_ = static_cast<quint16>(6000 + display);
    }

    QString xauth = QStandardPaths::findExecutable(QStringLiteral("xauth"));
#ifdef Q_OS_MACOS
    if (xauth.isEmpty()) xauth = QStandardPaths::findExecutable(QStringLiteral("xauth"), {QStringLiteral("/opt/X11/bin")});
#endif
    if (xauth.isEmpty()) {
        lastError_ = QStringLiteral("xauth was not found; install xauth (XQuartz on macOS)");
        return false;
    }

    QProcess process;
    // xauth honors XAUTHORITY, including Wayland/Xwayland's session-specific file.
    process.start(xauth, {QStringLiteral("list"), displayName_});
    if (!process.waitForFinished(3000)) {
        process.kill();
        process.waitForFinished();
        lastError_ = QStringLiteral("Failed to read X11 authority");
        return false;
    }
    if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
        static const QRegularExpression cookiePattern(QStringLiteral("^[0-9a-fA-F]{32}$"));
        for (const auto &line : process.readAllStandardOutput().split('\n')) {
            const auto fields = line.simplified().split(' ');
            if (fields.size() == 3 && fields[1] == "MIT-MAGIC-COOKIE-1"
                && cookiePattern.match(QString::fromLatin1(fields[2])).hasMatch()) {
                cookie_ = fields[2];
                break;
            }
        }
    }
    if (cookie_.isEmpty()) {
        lastError_ = QStringLiteral("No MIT-MAGIC-COOKIE-1 authority found for %1").arg(displayName_);
        return false;
    }

    const int socket = connectSocket();
    if (socket < 0) {
        lastError_ = QStringLiteral("Cannot connect to local X server at %1").arg(displayName_);
        return false;
    }
    ::close(socket);
    return true;
}

int UnixX11Display::connectSocket() const {
    // Duplicate the connected descriptor before Qt releases its ownership.
    if (!socketPath_.isEmpty()) {
        QLocalSocket socket;
        socket.connectToServer(socketPath_);
        if (!socket.waitForConnected(1000)) return -1;
        return ::dup(static_cast<int>(socket.socketDescriptor()));
    }
    QTcpSocket socket;
    socket.connectToHost(host_, port_);
    if (!socket.waitForConnected(1000)) return -1;
    socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);
    return ::dup(static_cast<int>(socket.socketDescriptor()));
}
