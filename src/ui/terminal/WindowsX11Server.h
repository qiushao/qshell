#ifndef QSHELL_WINDOWS_X11_SERVER_H
#define QSHELL_WINDOWS_X11_SERVER_H

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
    QString lastError() const;

private:
    WindowsX11Server() = default;
    WindowsX11Server(const WindowsX11Server &) = delete;
    WindowsX11Server &operator=(const WindowsX11Server &) = delete;

    QString findBundledExecutable() const;
    QString findBundledInstaller() const;
    QStringList candidateExecutables() const;
    QStringList candidateInstallers() const;
    QStringList launchArguments(const QString &executable, int displayNumber) const;
    QStringList installArguments(const QString &installDir) const;
    QString installedServerDir() const;
    bool installBundledServer();
    bool startBundledServer(const QString &executable, int displayNumber);
    bool waitForPort(quint16 port, int timeoutMs) const;
    bool isPortOpen(quint16 port, int timeoutMs) const;
    quint16 portForDisplay(int displayNumber) const;

    QProcess *process_ = nullptr;
    int displayNumber_ = -1;
    quint16 port_ = 0;
    bool available_ = false;
    QString lastError_;
};

#endif // QSHELL_WINDOWS_X11_SERVER_H
