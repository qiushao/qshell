#include "WindowsX11Server.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QFileInfoList>
#include <QHostAddress>
#include <QProcess>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QThread>

namespace {

constexpr int kX11BasePort = 6000;
constexpr int kFirstBundledDisplay = 44;
constexpr int kLastBundledDisplay = 99;
constexpr int kProbeTimeoutMs = 100;
constexpr int kExternalProbeTimeoutMs = 250;
constexpr int kProcessStartTimeoutMs = 5000;
constexpr int kInstallerTimeoutMs = 180000;
constexpr int kServerReadyTimeoutMs = 8000;

void addExecutableCandidate(QStringList &candidates, const QString &path)
{
    if (path.isEmpty()) return;

    QFileInfo info(QDir::fromNativeSeparators(path));
    if (info.isDir()) {
        candidates << QDir(info.absoluteFilePath()).filePath("vcxsrv.exe");
        candidates << QDir(info.absoluteFilePath()).filePath("XWin.exe");
        candidates << QDir(info.absoluteFilePath()).filePath("Xming.exe");
        return;
    }

    candidates << info.absoluteFilePath();
}

void addInstallerCandidates(QStringList &candidates, const QString &path)
{
    if (path.isEmpty()) return;

    QFileInfo info(QDir::fromNativeSeparators(path));
    if (info.isDir()) {
        const QStringList nameFilters = {
                QStringLiteral("vcxsrv*.installer.exe"),
                QStringLiteral("vcxsrv*.exe"),
                QStringLiteral("*.installer.exe"),
        };
        QFileInfoList entries = QDir(info.absoluteFilePath()).entryInfoList(nameFilters, QDir::Files, QDir::Name | QDir::Reversed);
        for (const QFileInfo &entry: entries) {
            candidates << entry.absoluteFilePath();
        }
        return;
    }

    candidates << info.absoluteFilePath();
}

} // namespace

WindowsX11Server &WindowsX11Server::instance()
{
    static WindowsX11Server server;
    return server;
}

bool WindowsX11Server::canForward()
{
    if (!findBundledExecutable().isEmpty() || !findBundledInstaller().isEmpty()) {
        lastError_.clear();
        return true;
    }

    if (isPortOpen(kX11BasePort, kExternalProbeTimeoutMs)) {
        lastError_.clear();
        return true;
    }

    lastError_ = QStringLiteral("No bundled X11 server, installer, or local X11 server is available");
    return false;
}

bool WindowsX11Server::ensureRunning()
{
    if (available_ && port_ != 0 && isPortOpen(port_, kProbeTimeoutMs)) {
        return true;
    }

    available_ = false;

    if (process_ && process_->state() == QProcess::Running && port_ != 0 && waitForPort(port_, 500)) {
        available_ = true;
        return true;
    }

    QString executable = findBundledExecutable();
    if (executable.isEmpty() && installBundledServer()) {
        executable = findBundledExecutable();
    }

    if (!executable.isEmpty()) {
        QString bundledStartError;
        for (int displayNumber = kFirstBundledDisplay; displayNumber <= kLastBundledDisplay; ++displayNumber) {
            const quint16 candidatePort = portForDisplay(displayNumber);
            if (isPortOpen(candidatePort, kProbeTimeoutMs)) {
                continue;
            }

            if (startBundledServer(executable, displayNumber)) {
                return true;
            }
            bundledStartError = lastError_;
        }

        lastError_ = bundledStartError.isEmpty()
                ? QStringLiteral("No free local display was available for the bundled X11 server")
                : bundledStartError;
    }

    if (isPortOpen(kX11BasePort, kExternalProbeTimeoutMs)) {
        displayNumber_ = 0;
        port_ = kX11BasePort;
        available_ = true;
        lastError_.clear();
        qDebug() << "Using existing local X11 server on 127.0.0.1:6000";
        return true;
    }

    if (lastError_.isEmpty()) {
        lastError_ = QStringLiteral("Bundled X11 server not found and no local X11 server is listening on 127.0.0.1:6000");
    }

    qWarning() << "X11 forwarding disabled:" << lastError_;
    return false;
}

QString WindowsX11Server::host() const
{
    return QStringLiteral("127.0.0.1");
}

quint16 WindowsX11Server::port() const
{
    return port_;
}

QString WindowsX11Server::displayName() const
{
    if (displayNumber_ < 0) return QString();
    return QStringLiteral(":%1").arg(displayNumber_);
}

QString WindowsX11Server::lastError() const
{
    return lastError_;
}

QString WindowsX11Server::findBundledExecutable() const
{
    const QStringList candidates = candidateExecutables();
    for (const QString &candidate: candidates) {
        QFileInfo info(candidate);
        if (info.exists() && info.isFile()) {
            return info.absoluteFilePath();
        }
    }

    return QString();
}

QString WindowsX11Server::findBundledInstaller() const
{
    const QStringList candidates = candidateInstallers();
    for (const QString &candidate: candidates) {
        QFileInfo info(candidate);
        if (info.exists() && info.isFile()) {
            return info.absoluteFilePath();
        }
    }

    return QString();
}

QStringList WindowsX11Server::candidateExecutables() const
{
    QStringList candidates;

    addExecutableCandidate(candidates, qEnvironmentVariable("QSHELL_X11_SERVER"));
    addExecutableCandidate(candidates, installedServerDir());

    const QString appDirPath = QCoreApplication::applicationDirPath();
    const QDir appDir(appDirPath);

    addExecutableCandidate(candidates, appDir.filePath("x11"));
    addExecutableCandidate(candidates, appDir.filePath("x11/vcxsrv"));
    addExecutableCandidate(candidates, appDir.filePath("VcXsrv"));
    addExecutableCandidate(candidates, appDir.filePath("vcxsrv"));

    return candidates;
}

QStringList WindowsX11Server::candidateInstallers() const
{
    QStringList candidates;

    addInstallerCandidates(candidates, qEnvironmentVariable("QSHELL_X11_INSTALLER"));

    const QString appDirPath = QCoreApplication::applicationDirPath();
    const QDir appDir(appDirPath);

    addInstallerCandidates(candidates, appDir.filePath("x11-installer"));
    addInstallerCandidates(candidates, appDir.filePath("vcxsrv-installer"));
    addInstallerCandidates(candidates, appDir.filePath("x11"));

    return candidates;
}

QStringList WindowsX11Server::launchArguments(const QString &executable, int displayNumber) const
{
    const QString executableName = QFileInfo(executable).fileName().toLower();
    QStringList arguments;

    arguments << QStringLiteral(":%1").arg(displayNumber);
    arguments << QStringLiteral("-multiwindow");
    arguments << QStringLiteral("-clipboard");
    arguments << QStringLiteral("-ac");
    arguments << QStringLiteral("-silent-dup-error");
    arguments << QStringLiteral("-noreset");
    arguments << QStringLiteral("-listen") << QStringLiteral("tcp");

    if (executableName == QStringLiteral("vcxsrv.exe")) {
        arguments << QStringLiteral("-wgl");
    }

    return arguments;
}

QStringList WindowsX11Server::installArguments(const QString &installDir) const
{
    return {
            QStringLiteral("/VERYSILENT"),
            QStringLiteral("/SUPPRESSMSGBOXES"),
            QStringLiteral("/NORESTART"),
            QStringLiteral("/SP-"),
            QStringLiteral("/NOICONS"),
            QStringLiteral("/DIR=%1").arg(QDir::toNativeSeparators(installDir)),
    };
}

QString WindowsX11Server::installedServerDir() const
{
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dataDir.isEmpty()) {
        dataDir = QDir::home().filePath(QStringLiteral("AppData/Local/qshell"));
    }

    return QDir(dataDir).filePath(QStringLiteral("x11/vcxsrv"));
}

bool WindowsX11Server::installBundledServer()
{
    const QString installer = findBundledInstaller();
    if (installer.isEmpty()) {
        return false;
    }

    const QString installDir = installedServerDir();
    if (!QDir().mkpath(installDir)) {
        lastError_ = QStringLiteral("Failed to create X11 server install directory: %1").arg(installDir);
        return false;
    }

    qDebug() << "Installing bundled X11 server to" << installDir;

    QProcess installerProcess;
    installerProcess.setProgram(installer);
    installerProcess.setArguments(installArguments(installDir));
    installerProcess.setWorkingDirectory(QFileInfo(installer).absolutePath());
    installerProcess.setProcessChannelMode(QProcess::MergedChannels);
    installerProcess.start();

    if (!installerProcess.waitForStarted(kProcessStartTimeoutMs)) {
        lastError_ = QStringLiteral("Failed to start bundled X11 installer: %1").arg(installerProcess.errorString());
        return false;
    }

    if (!installerProcess.waitForFinished(kInstallerTimeoutMs)) {
        installerProcess.kill();
        installerProcess.waitForFinished(1000);
        lastError_ = QStringLiteral("Timed out installing bundled X11 server");
        return false;
    }

    if (installerProcess.exitStatus() != QProcess::NormalExit || installerProcess.exitCode() != 0) {
        const QString output = QString::fromLocal8Bit(installerProcess.readAll());
        lastError_ = QStringLiteral("Bundled X11 installer failed with exit code %1").arg(installerProcess.exitCode());
        if (!output.isEmpty()) {
            qWarning() << "Bundled X11 installer output:" << output;
        }
        return false;
    }

    if (findBundledExecutable().isEmpty()) {
        lastError_ = QStringLiteral("Bundled X11 installer completed but vcxsrv.exe was not found in %1").arg(installDir);
        return false;
    }

    qDebug() << "Bundled X11 server installed";
    return true;
}

bool WindowsX11Server::startBundledServer(const QString &executable, int displayNumber)
{
    if (process_ && process_->state() != QProcess::NotRunning) {
        process_->terminate();
        if (!process_->waitForFinished(1000)) {
            process_->kill();
            process_->waitForFinished(1000);
        }
    }

    if (process_) {
        process_->deleteLater();
        process_ = nullptr;
    }

    process_ = new QProcess(QCoreApplication::instance());
    process_->setProgram(executable);
    process_->setArguments(launchArguments(executable, displayNumber));
    process_->setWorkingDirectory(QFileInfo(executable).absolutePath());
    process_->setProcessChannelMode(QProcess::MergedChannels);
    process_->start();

    if (!process_->waitForStarted(kProcessStartTimeoutMs)) {
        lastError_ = QStringLiteral("Failed to start bundled X11 server: %1").arg(process_->errorString());
        process_->deleteLater();
        process_ = nullptr;
        return false;
    }

    displayNumber_ = displayNumber;
    port_ = portForDisplay(displayNumber);

    if (!waitForPort(port_, kServerReadyTimeoutMs)) {
        const QByteArray output = process_->readAll();
        lastError_ = QStringLiteral("Bundled X11 server did not open %1:%2").arg(host()).arg(port_);
        if (!output.isEmpty()) {
            qWarning() << "Bundled X11 server output:" << QString::fromLocal8Bit(output);
        }
        if (process_->state() != QProcess::NotRunning) {
            process_->terminate();
            if (!process_->waitForFinished(1000)) {
                process_->kill();
                process_->waitForFinished(1000);
            }
        }
        process_->deleteLater();
        process_ = nullptr;
        displayNumber_ = -1;
        port_ = 0;
        return false;
    }

    available_ = true;
    lastError_.clear();
    qDebug() << "Bundled X11 server started on" << displayName() << "port" << port_;
    return true;
}

bool WindowsX11Server::waitForPort(quint16 port, int timeoutMs) const
{
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeoutMs) {
        if (isPortOpen(port, kProbeTimeoutMs)) {
            return true;
        }

        if (process_ && process_->state() == QProcess::NotRunning) {
            break;
        }

        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
        QThread::msleep(50);
    }

    return isPortOpen(port, kProbeTimeoutMs);
}

bool WindowsX11Server::isPortOpen(quint16 port, int timeoutMs) const
{
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    const bool connected = socket.waitForConnected(timeoutMs);
    if (connected) {
        socket.disconnectFromHost();
        socket.waitForDisconnected(50);
    }
    return connected;
}

quint16 WindowsX11Server::portForDisplay(int displayNumber) const
{
    return static_cast<quint16>(kX11BasePort + displayNumber);
}
