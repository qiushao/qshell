#ifndef QSHELL_WINDOWS_X11_SERVER_H
#define QSHELL_WINDOWS_X11_SERVER_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QtGlobal>

class QProcess;

class WindowsX11Server {
public:
    static WindowsX11Server &instance();

    bool canForward();
    bool ensureRunning();
    QString host() const;
    quint16 port() const;
    QString displayName() const;
    QByteArray authenticationProtocol() const;
    QByteArray authenticationCookie() const;
    QString lastError() const;

private:
    WindowsX11Server() = default;
    WindowsX11Server(const WindowsX11Server &) = delete;
    WindowsX11Server &operator=(const WindowsX11Server &) = delete;

    QString findPackagedExecutable() const;
    QString installedExecutable() const;
    QString findBundledArchive() const;
    QString findBundledExtractor() const;
    QStringList packagedExecutableCandidates() const;
    QStringList runtimeDirectoryCandidates() const;
    QStringList launchArguments(const QString &executable, int displayNumber) const;
    QString installedServerDir() const;
    bool ensureBundledServerExtracted();
    bool createAuthorityFile(int displayNumber);
    bool startBundledServer(const QString &executable, int displayNumber);
    void shutdown();
    bool waitForPort(quint16 port, int timeoutMs) const;
    bool isPortOpen(quint16 port, int timeoutMs) const;
    quint16 portForDisplay(int displayNumber) const;

    QProcess *process_ = nullptr;
    int displayNumber_ = -1;
    quint16 port_ = 0;
    bool available_ = false;
    QByteArray authenticationCookie_;
    QString authorityFile_;
    QString lastError_;
    bool shutdownHookInstalled_ = false;
};

#endif// QSHELL_WINDOWS_X11_SERVER_H
