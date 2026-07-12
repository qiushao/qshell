#include "WindowsX11Server.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QHostAddress>
#include <QProcess>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTcpSocket>
#include <QThread>

#include <cstring>
#include <windows.h>

namespace {

constexpr int kX11BasePort = 6000;
constexpr int kFirstBundledDisplay = 44;
constexpr int kLastBundledDisplay = 99;
constexpr int kProbeTimeoutMs = 100;
constexpr int kExternalProbeTimeoutMs = 250;
constexpr int kProcessStartTimeoutMs = 5000;
constexpr int kExtractionTimeoutMs = 180000;
constexpr int kServerReadyTimeoutMs = 8000;
constexpr quint16 kFamilyLocal = 256;
constexpr qsizetype kX11CookieBytes = 16;
const QByteArray kX11AuthProtocol("MIT-MAGIC-COOKIE-1");

void addExecutableCandidate(QStringList &candidates, const QString &path) {
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

QString firstExistingFile(const QStringList &candidates) {
    for (const QString &candidate: candidates) {
        const QFileInfo info(candidate);
        if (info.exists() && info.isFile()) {
            return info.absoluteFilePath();
        }
    }
    return QString();
}

void configureHiddenProcess(QProcess &process) {
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
        arguments->flags |= CREATE_NO_WINDOW;
    });
}

void appendUInt16(QByteArray &data, quint16 value) {
    data.append(static_cast<char>((value >> 8) & 0xff));
    data.append(static_cast<char>(value & 0xff));
}

bool appendXAuthorityField(QByteArray &data, const QByteArray &field) {
    if (field.size() > 0xffff) return false;
    appendUInt16(data, static_cast<quint16>(field.size()));
    data.append(field);
    return true;
}

}// namespace

WindowsX11Server &WindowsX11Server::instance() {
    static WindowsX11Server server;
    return server;
}

bool WindowsX11Server::canForward() {
    if (!findPackagedExecutable().isEmpty() || !installedExecutable().isEmpty()) {
        lastError_.clear();
        return true;
    }

    if (!findBundledArchive().isEmpty() && !findBundledExtractor().isEmpty()) {
        lastError_.clear();
        return true;
    }

    if (isPortOpen(kX11BasePort, kExternalProbeTimeoutMs)) {
        lastError_.clear();
        return true;
    }

    lastError_ = QStringLiteral("No bundled X11 runtime or local X11 server is available");
    return false;
}

bool WindowsX11Server::ensureRunning() {
    if (available_ && port_ != 0 && isPortOpen(port_, kProbeTimeoutMs)) {
        return true;
    }

    available_ = false;

    if (process_ && process_->state() == QProcess::Running && port_ != 0 && waitForPort(port_, 500)) {
        available_ = true;
        return true;
    }

    QString executable = findPackagedExecutable();
    if (executable.isEmpty()) {
        const QString archive = findBundledArchive();
        const QString extractor = findBundledExtractor();
        if (!archive.isEmpty() && !extractor.isEmpty()) {
            if (ensureBundledServerExtracted()) {
                executable = installedExecutable();
            }
        } else {
            executable = installedExecutable();
        }
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
        authenticationCookie_.clear();
        if (!authorityFile_.isEmpty()) QFile::remove(authorityFile_);
        authorityFile_.clear();
        lastError_.clear();
        qDebug() << "Using existing local X11 server on 127.0.0.1:6000";
        return true;
    }

    if (lastError_.isEmpty()) {
        lastError_ = QStringLiteral("Bundled X11 runtime not found and no local X11 server is listening on 127.0.0.1:6000");
    }

    qWarning() << "X11 forwarding disabled:" << lastError_;
    return false;
}

QString WindowsX11Server::host() const {
    return QStringLiteral("127.0.0.1");
}

quint16 WindowsX11Server::port() const {
    return port_;
}

QString WindowsX11Server::displayName() const {
    if (displayNumber_ < 0) return QString();
    return QStringLiteral(":%1").arg(displayNumber_);
}

QByteArray WindowsX11Server::authenticationProtocol() const {
    return authenticationCookie_.isEmpty() ? QByteArray() : kX11AuthProtocol;
}

QByteArray WindowsX11Server::authenticationCookie() const {
    return authenticationCookie_;
}

QString WindowsX11Server::lastError() const {
    return lastError_;
}

QString WindowsX11Server::findPackagedExecutable() const {
    return firstExistingFile(packagedExecutableCandidates());
}

QString WindowsX11Server::installedExecutable() const {
    return firstExistingFile({
            QDir(installedServerDir()).filePath(QStringLiteral("vcxsrv.exe")),
            QDir(installedServerDir()).filePath(QStringLiteral("XWin.exe")),
            QDir(installedServerDir()).filePath(QStringLiteral("Xming.exe")),
    });
}

QString WindowsX11Server::findBundledArchive() const {
    const QString overridePath = qEnvironmentVariable("QSHELL_X11_ARCHIVE");
    if (!overridePath.isEmpty()) {
        const QFileInfo overrideInfo(QDir::fromNativeSeparators(overridePath));
        if (overrideInfo.exists() && overrideInfo.isFile()) return overrideInfo.absoluteFilePath();
    }

    for (const QString &directory: runtimeDirectoryCandidates()) {
        const QFileInfoList archives = QDir(directory).entryInfoList(
                {QStringLiteral("vcxsrv*.7z"), QStringLiteral("x11-runtime*.7z")},
                QDir::Files, QDir::Name | QDir::Reversed);
        if (!archives.isEmpty()) return archives.constFirst().absoluteFilePath();
    }

    return QString();
}

QString WindowsX11Server::findBundledExtractor() const {
    const QString overridePath = qEnvironmentVariable("QSHELL_X11_EXTRACTOR");
    if (!overridePath.isEmpty()) {
        const QFileInfo overrideInfo(QDir::fromNativeSeparators(overridePath));
        if (overrideInfo.exists() && overrideInfo.isFile()) return overrideInfo.absoluteFilePath();
    }

    QStringList candidates;
    for (const QString &directory: runtimeDirectoryCandidates()) {
        candidates << QDir(directory).filePath(QStringLiteral("7zr.exe"));
        candidates << QDir(directory).filePath(QStringLiteral("7za.exe"));
        candidates << QDir(directory).filePath(QStringLiteral("7z.exe"));
    }
    return firstExistingFile(candidates);
}

QStringList WindowsX11Server::packagedExecutableCandidates() const {
    QStringList candidates;
    addExecutableCandidate(candidates, qEnvironmentVariable("QSHELL_X11_SERVER"));

    const QDir appDir(QCoreApplication::applicationDirPath());
    addExecutableCandidate(candidates, appDir.filePath("x11"));
    addExecutableCandidate(candidates, appDir.filePath("x11/vcxsrv"));
    addExecutableCandidate(candidates, appDir.filePath("VcXsrv"));
    addExecutableCandidate(candidates, appDir.filePath("vcxsrv"));
    return candidates;
}

QStringList WindowsX11Server::runtimeDirectoryCandidates() const {
    QStringList candidates;
    const QString overrideDirectory = qEnvironmentVariable("QSHELL_X11_RUNTIME");
    if (!overrideDirectory.isEmpty()) candidates << QDir::fromNativeSeparators(overrideDirectory);

    const QDir appDir(QCoreApplication::applicationDirPath());
    candidates << appDir.filePath(QStringLiteral("x11-runtime"));
    candidates << appDir.filePath(QStringLiteral("x11"));
    candidates.removeDuplicates();
    return candidates;
}

QStringList WindowsX11Server::launchArguments(const QString &executable, int displayNumber) const {
    const QString executableName = QFileInfo(executable).fileName().toLower();
    QStringList arguments;

    arguments << QStringLiteral(":%1").arg(displayNumber);
    arguments << QStringLiteral("-multiwindow");
    arguments << QStringLiteral("-clipboard");
    arguments << QStringLiteral("-silent-dup-error");
    arguments << QStringLiteral("-noreset");
    arguments << QStringLiteral("-listen") << QStringLiteral("tcp");
    if (!authorityFile_.isEmpty()) {
        arguments << QStringLiteral("-auth") << QDir::toNativeSeparators(authorityFile_);
    }

    if (executableName == QStringLiteral("vcxsrv.exe")) {
        arguments << QStringLiteral("-wgl");
    }

    return arguments;
}

QString WindowsX11Server::installedServerDir() const {
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dataDir.isEmpty()) {
        dataDir = QDir::home().filePath(QStringLiteral("AppData/Local/qshell"));
    }
    return QDir(dataDir).filePath(QStringLiteral("x11/vcxsrv"));
}

bool WindowsX11Server::ensureBundledServerExtracted() {
    const QString archive = findBundledArchive();
    const QString extractor = findBundledExtractor();
    if (archive.isEmpty() || extractor.isEmpty()) return false;

    const QFileInfo archiveInfo(archive);
    const QString runtimeId = QStringLiteral("%1:%2")
                                      .arg(archiveInfo.fileName())
                                      .arg(archiveInfo.size());
    const QString installDir = installedServerDir();
    const QString markerPath = QDir(installDir).filePath(QStringLiteral(".qshell-runtime"));

    QFile marker(markerPath);
    if (!installedExecutable().isEmpty() && marker.open(QIODevice::ReadOnly) && QString::fromUtf8(marker.readAll()).trimmed() == runtimeId) {
        return true;
    }

    const QString parentDir = QFileInfo(installDir).absolutePath();
    if (!QDir().mkpath(parentDir)) {
        lastError_ = QStringLiteral("Failed to create X11 runtime directory: %1").arg(parentDir);
        return false;
    }

    const QString stagingDir = QDir(parentDir).filePath(
            QStringLiteral("vcxsrv.tmp-%1").arg(QCoreApplication::applicationPid()));
    QDir staging(stagingDir);
    if (staging.exists() && !staging.removeRecursively()) {
        lastError_ = QStringLiteral("Failed to clean temporary X11 runtime directory: %1").arg(stagingDir);
        return false;
    }
    if (!QDir().mkpath(stagingDir)) {
        lastError_ = QStringLiteral("Failed to create temporary X11 runtime directory: %1").arg(stagingDir);
        return false;
    }

    qDebug() << "Extracting bundled X11 runtime to" << stagingDir;
    QProcess extractionProcess;
    configureHiddenProcess(extractionProcess);
    extractionProcess.setProgram(extractor);
    extractionProcess.setArguments({
            QStringLiteral("x"),
            QDir::toNativeSeparators(archive),
            QStringLiteral("-o%1").arg(QDir::toNativeSeparators(stagingDir)),
            QStringLiteral("-y"),
            QStringLiteral("-bso0"),
            QStringLiteral("-bsp0"),
    });
    extractionProcess.setWorkingDirectory(QFileInfo(extractor).absolutePath());
    extractionProcess.setProcessChannelMode(QProcess::MergedChannels);
    extractionProcess.start();

    if (!extractionProcess.waitForStarted(kProcessStartTimeoutMs)) {
        staging.removeRecursively();
        lastError_ = QStringLiteral("Failed to start bundled X11 extractor: %1").arg(extractionProcess.errorString());
        return false;
    }
    if (!extractionProcess.waitForFinished(kExtractionTimeoutMs)) {
        extractionProcess.kill();
        extractionProcess.waitForFinished(1000);
        staging.removeRecursively();
        lastError_ = QStringLiteral("Timed out extracting bundled X11 runtime");
        return false;
    }
    if (extractionProcess.exitStatus() != QProcess::NormalExit || extractionProcess.exitCode() != 0) {
        const QString output = QString::fromLocal8Bit(extractionProcess.readAll());
        staging.removeRecursively();
        lastError_ = QStringLiteral("Bundled X11 extraction failed with exit code %1: %2")
                             .arg(extractionProcess.exitCode())
                             .arg(output.trimmed());
        return false;
    }
    if (!QFileInfo::exists(QDir(stagingDir).filePath(QStringLiteral("vcxsrv.exe")))) {
        staging.removeRecursively();
        lastError_ = QStringLiteral("Bundled X11 archive does not contain vcxsrv.exe");
        return false;
    }

    QDir installed(installDir);
    if (installed.exists() && !installed.removeRecursively()) {
        staging.removeRecursively();
        lastError_ = QStringLiteral("Failed to replace the installed X11 runtime: %1").arg(installDir);
        return false;
    }
    if (!QDir().rename(stagingDir, installDir)) {
        staging.removeRecursively();
        lastError_ = QStringLiteral("Failed to activate the extracted X11 runtime: %1").arg(installDir);
        return false;
    }

    QSaveFile runtimeMarker(markerPath);
    if (!runtimeMarker.open(QIODevice::WriteOnly) || runtimeMarker.write(runtimeId.toUtf8()) < 0 || !runtimeMarker.commit()) {
        lastError_ = QStringLiteral("Failed to write the X11 runtime version marker");
        return false;
    }

    qDebug() << "Bundled X11 runtime extracted successfully";
    return true;
}

bool WindowsX11Server::createAuthorityFile(int displayNumber) {
    if (!authorityFile_.isEmpty()) {
        QFile::remove(authorityFile_);
        authorityFile_.clear();
    }
    authenticationCookie_.clear();

    QByteArray rawCookie(kX11CookieBytes, Qt::Uninitialized);
    for (qsizetype offset = 0; offset < rawCookie.size(); offset += sizeof(quint32)) {
        const quint32 randomValue = QRandomGenerator::system()->generate();
        const qsizetype bytesToCopy = qMin<qsizetype>(sizeof(randomValue), rawCookie.size() - offset);
        std::memcpy(rawCookie.data() + offset, &randomValue, static_cast<size_t>(bytesToCopy));
    }

    QByteArray authority;
    appendUInt16(authority, kFamilyLocal);
    if (!appendXAuthorityField(authority, QSysInfo::machineHostName().toUtf8()) || !appendXAuthorityField(authority, QByteArray::number(displayNumber)) || !appendXAuthorityField(authority, kX11AuthProtocol) || !appendXAuthorityField(authority, rawCookie)) {
        lastError_ = QStringLiteral("Failed to encode the X11 authority record");
        return false;
    }

    const QString authorityDir = QFileInfo(installedServerDir()).absolutePath();
    if (!QDir().mkpath(authorityDir)) {
        lastError_ = QStringLiteral("Failed to create X11 authority directory: %1").arg(authorityDir);
        return false;
    }

    authorityFile_ = QDir(authorityDir).filePath(QStringLiteral(".Xauthority-%1-%2").arg(QCoreApplication::applicationPid()).arg(displayNumber));
    QSaveFile file(authorityFile_);
    if (!file.open(QIODevice::WriteOnly) || file.write(authority) != authority.size() || !file.commit()) {
        lastError_ = QStringLiteral("Failed to create X11 authority file: %1").arg(authorityFile_);
        authorityFile_.clear();
        authenticationCookie_.clear();
        return false;
    }
    QFile::setPermissions(authorityFile_, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    authenticationCookie_ = rawCookie.toHex();
    return true;
}

bool WindowsX11Server::startBundledServer(const QString &executable, int displayNumber) {
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

    if (!createAuthorityFile(displayNumber)) return false;

    process_ = new QProcess(QCoreApplication::instance());
    if (!shutdownHookInstalled_) {
        QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
                         QCoreApplication::instance(), [this]() { shutdown(); });
        shutdownHookInstalled_ = true;
    }
    configureHiddenProcess(*process_);
    process_->setProgram(executable);
    process_->setArguments(launchArguments(executable, displayNumber));
    process_->setWorkingDirectory(QFileInfo(executable).absolutePath());
    process_->setProcessChannelMode(QProcess::MergedChannels);
    process_->start();

    if (!process_->waitForStarted(kProcessStartTimeoutMs)) {
        lastError_ = QStringLiteral("Failed to start bundled X11 server: %1").arg(process_->errorString());
        process_->deleteLater();
        process_ = nullptr;
        authenticationCookie_.clear();
        QFile::remove(authorityFile_);
        authorityFile_.clear();
        return false;
    }

    displayNumber_ = displayNumber;
    port_ = portForDisplay(displayNumber);
    if (!waitForPort(port_, kServerReadyTimeoutMs)) {
        const QByteArray output = process_->readAll();
        lastError_ = QStringLiteral("Bundled X11 server did not open %1:%2").arg(host()).arg(port_);
        if (!output.isEmpty()) qWarning() << "Bundled X11 server output:" << QString::fromLocal8Bit(output);
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
        authenticationCookie_.clear();
        QFile::remove(authorityFile_);
        authorityFile_.clear();
        return false;
    }

    available_ = true;
    lastError_.clear();
    qDebug() << "Bundled X11 server started on" << displayName() << "port" << port_;
    return true;
}

void WindowsX11Server::shutdown() {
    available_ = false;
    if (process_ && process_->state() != QProcess::NotRunning) {
        process_->terminate();
        if (!process_->waitForFinished(1000)) {
            process_->kill();
            process_->waitForFinished(1000);
        }
    }
    if (process_) {
        delete process_;
        process_ = nullptr;
    }
    if (!authorityFile_.isEmpty()) {
        QFile::remove(authorityFile_);
        authorityFile_.clear();
    }
    authenticationCookie_.clear();
    displayNumber_ = -1;
    port_ = 0;
}

bool WindowsX11Server::waitForPort(quint16 port, int timeoutMs) const {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (isPortOpen(port, kProbeTimeoutMs)) return true;
        if (process_ && process_->state() == QProcess::NotRunning) break;
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
        QThread::msleep(50);
    }
    return isPortOpen(port, kProbeTimeoutMs);
}

bool WindowsX11Server::isPortOpen(quint16 port, int timeoutMs) const {
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    const bool connected = socket.waitForConnected(timeoutMs);
    if (connected) {
        socket.disconnectFromHost();
        socket.waitForDisconnected(50);
    }
    return connected;
}

quint16 WindowsX11Server::portForDisplay(int displayNumber) const {
    return static_cast<quint16>(kX11BasePort + displayNumber);
}
