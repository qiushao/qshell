#include "ui/terminal/UnixX11Display.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <cstdlib>
#include <sys/socket.h>
#include <unistd.h>

namespace {
QString xauthExecutable;

void require(bool condition, const char *message) {
    if (!condition) {
        qCritical() << message;
        std::exit(1);
    }
}

void addAuthority(const QString &display, const QByteArray &cookie) {
    QProcess xauth;
    xauth.start(xauthExecutable, {QStringLiteral("add"), display,
                                        QStringLiteral("MIT-MAGIC-COOKIE-1"), QString::fromLatin1(cookie)});
    require(xauth.waitForFinished() && xauth.exitCode() == 0, "cannot create test Xauthority");
}

template<typename Socket>
void exchange(int descriptor, Socket *server) {
    require(server != nullptr, "local server did not accept the bridge");
    require(::send(descriptor, "request", 7, 0) == 7, "cannot send to local X server");
    require(server->waitForReadyRead(1000) && server->readAll() == "request", "local X server received wrong bytes");
    server->write("reply");
    require(server->waitForBytesWritten(1000), "cannot send local X reply");
    char reply[5];
    require(::recv(descriptor, reply, sizeof(reply), 0) == 5 && QByteArray(reply, 5) == "reply",
            "bridge received wrong reply");
    ::close(descriptor);
    delete server;
}
}// namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    xauthExecutable = QStandardPaths::findExecutable(QStringLiteral("xauth"));
#ifdef Q_OS_MACOS
    if (xauthExecutable.isEmpty()) xauthExecutable = QStandardPaths::findExecutable(QStringLiteral("xauth"), {QStringLiteral("/opt/X11/bin")});
#endif
    if (xauthExecutable.isEmpty()) {
        qInfo() << "X11 display test requires xauth";
        return 77;
    }
    QTemporaryDir directory;
    require(directory.isValid(), "cannot create temporary directory");
    const QString authority = directory.filePath("authority");
    QFile file(authority);
    require(file.open(QIODevice::WriteOnly), "cannot create authority file");
    file.close();
    qputenv("XAUTHORITY", authority.toUtf8());

    UnixX11Display display;
    qunsetenv("DISPLAY");
    require(!display.prepare() && !display.lastError().isEmpty(), "missing DISPLAY was accepted");
    for (const auto *invalid : {"broken", ":999999", ":0.bad", ":0.999999999999"}) {
        qputenv("DISPLAY", invalid);
        require(!display.prepare(), "invalid DISPLAY was accepted");
    }

    const QByteArray cookie("0123456789abcdef0123456789abcdef");
    // Exercise XQuartz's absolute launchd path without requiring a macOS server.
    const QString socketPath = directory.filePath("org.xquartz:93");
    QLocalServer local;
    require(local.listen(socketPath), "cannot listen on Unix socket");
    qputenv("DISPLAY", (socketPath + ".2").toUtf8());
    addAuthority(socketPath, cookie);
    require(display.prepare(), qPrintable(display.lastError()));
    require(display.screen() == 2 && display.authenticationCookie() == cookie,
            "screen or Xauthority cookie was not preserved");
    require(local.waitForNewConnection(1000), "Unix probe was not accepted");
    delete local.nextPendingConnection();
    int descriptor = display.connectSocket();
    require(descriptor >= 0 && local.waitForNewConnection(1000), "Unix bridge failed");
    exchange(descriptor, local.nextPendingConnection());

    QTcpServer tcp;
    require(tcp.listen(QHostAddress::LocalHost), "cannot listen on TCP socket");
    require(tcp.serverPort() > 6000, "test TCP port cannot represent an X11 display");
    const QString tcpDisplay = QStringLiteral("127.0.0.1:%1.1").arg(tcp.serverPort() - 6000);
    qputenv("DISPLAY", tcpDisplay.toUtf8());
    addAuthority(tcpDisplay, cookie);
    require(display.prepare(), qPrintable(display.lastError()));
    require(display.screen() == 1 && display.authenticationCookie() == cookie, "TCP display authority mismatch");
    require(tcp.waitForNewConnection(1000), "TCP probe was not accepted");
    delete tcp.nextPendingConnection();
    descriptor = display.connectSocket();
    require(descriptor >= 0 && tcp.waitForNewConnection(1000), "TCP bridge failed");
    exchange(descriptor, tcp.nextPendingConnection());

    tcp.close();
    require(!display.prepare(), "unavailable X server was accepted");
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "cannot clear authority");
    file.close();
    qputenv("DISPLAY", socketPath.toUtf8());
    require(!display.prepare() && display.authenticationCookie().isEmpty(), "stale authority was reused");
    qInfo() << "Unix/TCP X11 connections, XQuartz paths, screens and Xauthority passed";
    return 0;
}
