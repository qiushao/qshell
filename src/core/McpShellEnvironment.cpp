#include "McpShellEnvironment.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QRegularExpression>

namespace {
struct ShellConfiguration {
    QString startupFilePath;
    QString environmentAssignment;
    QString sourceCommand;
};

QString quoteForShell(QString value) {
    value.replace("'", "'\"'\"'");
    return "'" + value + "'";
}

bool resolveShellConfiguration(const QString &bearerToken,
                               ShellConfiguration *configuration,
                               QString *errorMessage) {
    const QString shellPath = qEnvironmentVariable("SHELL").trimmed();
    const QString shellName = QFileInfo(shellPath).fileName().toLower();
    const QString homePath = QDir::homePath();

    configuration->environmentAssignment = "export QSHELL_MCP_TOKEN=" + quoteForShell(bearerToken);
    configuration->sourceCommand = "source \"$HOME/.config/qshell/mcp.env\"";

    if (shellName.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("SHELL is not set; configure QSHELL_MCP_TOKEN manually.");
        }
        return false;
    }

    if (shellName == "bash") {
        configuration->startupFilePath = QDir(homePath).filePath(".bashrc");
    } else if (shellName == "zsh") {
        configuration->startupFilePath = QDir(homePath).filePath(".zshrc");
    } else if (shellName == "fish") {
        configuration->startupFilePath = QDir(homePath).filePath(".config/fish/config.fish");
        configuration->environmentAssignment = "set -gx QSHELL_MCP_TOKEN " + quoteForShell(bearerToken);
    } else if (shellName == "ksh" || shellName == "mksh") {
        configuration->startupFilePath = QDir(homePath).filePath(".kshrc");
    } else if (shellName == "sh" || shellName == "dash" || shellName == "ash") {
        configuration->startupFilePath = QDir(homePath).filePath(".profile");
        configuration->sourceCommand = ". \"$HOME/.config/qshell/mcp.env\"";
    } else {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("Unsupported shell '%1'; configure QSHELL_MCP_TOKEN manually.").arg(shellName);
        }
        return false;
    }

    return true;
}

bool containsMcpEnvironmentSource(const QByteArray &content) {
    static const QRegularExpression sourcePattern(
            R"(\bsource\s+[^#\r\n]*mcp\.env|(?:^|[;\s])\.\s+[^#\r\n]*mcp\.env)");
    const QStringList lines = QString::fromUtf8(content).split('\n');
    for (const QString &line: lines) {
        const QString trimmedLine = line.trimmed();
        if (trimmedLine.startsWith('#') || !trimmedLine.contains("mcp.env")) {
            continue;
        }
        const QString commandText = trimmedLine.section('#', 0, 0);
        if (sourcePattern.match(commandText).hasMatch()) {
            return true;
        }
    }
    return false;
}

bool writeEnvironmentFile(const QString &filePath,
                          const QString &environmentAssignment,
                          QString *errorMessage) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("Cannot write MCP environment file: %1").arg(file.errorString());
        }
        return false;
    }

#if defined(Q_OS_UNIX)
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("Cannot restrict MCP environment file permissions: %1").arg(file.errorString());
        }
        file.close();
        return false;
    }
#endif

    const QByteArray content = (environmentAssignment + "\n").toUtf8();
    if (file.write(content) != content.size()) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("Cannot write MCP environment file: %1").arg(file.errorString());
        }
        file.close();
        return false;
    }
    file.close();
    return true;
}

bool updateShellStartupFile(const ShellConfiguration &configuration, QString *errorMessage) {
    const QFileInfo startupFileInfo(configuration.startupFilePath);
    QDir startupDirectory = startupFileInfo.dir();
    if (!startupDirectory.exists() && !startupDirectory.mkpath(".")) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("Cannot create shell configuration directory: %1").arg(startupDirectory.path());
        }
        return false;
    }

    QFile startupFile(configuration.startupFilePath);
    if (!startupFile.open(QIODevice::ReadWrite | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("Cannot update shell startup file: %1").arg(startupFile.errorString());
        }
        return false;
    }

    const QByteArray existingContent = startupFile.readAll();
    if (containsMcpEnvironmentSource(existingContent)) {
        startupFile.close();
        return true;
    }

    QByteArray addition;
    if (!existingContent.isEmpty() && !existingContent.endsWith('\n')) {
        addition.append('\n');
    }
    addition.append("# QShell MCP environment\n");
    addition.append(configuration.sourceCommand.toUtf8());
    addition.append('\n');

    if (!startupFile.seek(startupFile.size()) || startupFile.write(addition) != addition.size()) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("Cannot update shell startup file: %1").arg(startupFile.errorString());
        }
        startupFile.close();
        return false;
    }

    startupFile.close();
    return true;
}
}// namespace

bool McpShellEnvironment::configure(const QString &bearerToken, QString *errorMessage) {
    if (bearerToken.isEmpty() || bearerToken.contains('\n') || bearerToken.contains('\r')) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("The MCP bearer token is empty or contains a line break.");
        }
        return false;
    }

    ShellConfiguration configuration;
    const bool shellResolved = resolveShellConfiguration(bearerToken, &configuration, errorMessage);

    const QString environmentDirectoryPath = QDir(QDir::homePath()).filePath(".config/qshell");
    QDir environmentDirectory(environmentDirectoryPath);
    if (!environmentDirectory.exists() && !environmentDirectory.mkpath(".")) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("Cannot create MCP environment directory: %1").arg(environmentDirectoryPath);
        }
        return false;
    }

    const QString environmentFilePath = environmentDirectory.filePath("mcp.env");
    if (!writeEnvironmentFile(environmentFilePath, configuration.environmentAssignment, errorMessage)) {
        return false;
    }

    if (!shellResolved) {
        return false;
    }

    return updateShellStartupFile(configuration, errorMessage);
}
