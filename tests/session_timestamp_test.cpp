#include "core/ConfigManager.h"
#include "ui/terminal/BaseTerminal.h"

#include <QApplication>
#include <QDebug>
#include <cstdlib>
#include <QTemporaryDir>

namespace {
class TestTerminal : public BaseTerminal {
public:
    explicit TestTerminal(ProtocolType protocol) : BaseTerminal(nullptr) {
        sessionData_.protocolType = protocol;
        sessionData_.name = QString::number(static_cast<int>(protocol));
    }
    void connect() override {}
    void disconnect() override {}
    void writeToBackend(const QByteArray &) override {}
    using BaseTerminal::receiveBackendData;
};

void require(bool condition, const char *message) {
    if (!condition) {
        qCritical() << message;
        std::exit(1);
    }
}
}

int main(int argc, char *argv[]) {
    QTemporaryDir directory;
    require(directory.isValid(), "could not create temporary log directory");
    qputenv("XDG_CONFIG_HOME", directory.path().toUtf8());
    QApplication application(argc, argv);
    Q_INIT_RESOURCE(qtermwidget);
    QApplication::setApplicationName(QStringLiteral("qshell-timestamp-test"));
    GlobalSettings settings;
    settings.autoSaveLog = true;
    settings.autoSaveLogDirectory = directory.path();
    settings.terminalTimestamp = true;
    auto *config = ConfigManager::instance();
    config->setGlobalSettings(settings);

    for (const auto protocol : {ProtocolType::Serial, ProtocolType::SSH, ProtocolType::LocalShell}) {
        QString logPath;
        QString timestamp;
        {
            TestTerminal terminal(protocol);
            auto *gutter = terminal.findChild<QWidget *>(QStringLiteral("terminalTimestampDisplay"));
            require(gutter && !gutter->isHidden(), "session type has no timestamp gutter");
            QObject::connect(&terminal, &QTermWidget::onNewLineWithTimestamp,
                             [&](const QString &line, const QString &time) {
                                 if (line == QStringLiteral("enabled"))
                                     timestamp = time;
                             });
            terminal.startAutoLogging();
            require(terminal.isLogging(), "automatic logging did not start");
            logPath = terminal.logFilePath();
            terminal.receiveBackendData("enabled\r\n");
            QApplication::processEvents();
            require(!timestamp.isEmpty(), "session backend output has no timestamp");
            settings.terminalTimestamp = false;
            config->setGlobalSettings(settings);
            require(gutter->isHidden(), "setting change did not update existing session");
            terminal.receiveBackendData("disabled\r\n");
            QApplication::processEvents();
            settings.terminalTimestamp = true;
            config->setGlobalSettings(settings);
            require(!gutter->isHidden(), "setting change did not restore gutter");
        }
        QFile log(logPath);
        require(log.open(QIODevice::ReadOnly), "could not read saved log");
        const QString content = QString::fromUtf8(log.readAll());
        require(content.contains(timestamp + QStringLiteral(" enabled\n")),
                "saved log did not reuse terminal timestamp");
        require(content.contains(QStringLiteral("\ndisabled\n")),
                "saved log retained a timestamp while disabled");
    }
    return 0;
}
