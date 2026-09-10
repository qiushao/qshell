#include "core/ConfigManager.h"
#include "ui/MainWindow.h"
#include "ui/command/CommandButtonBar.h"
#include "ui/command/CommandWindow.h"
#include "ui/terminal/BaseTerminal.h"

#include <QApplication>
#include <QDebug>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTextEdit>
#include <cstdlib>

namespace {
void require(bool condition, const char *message) {
    if (!condition) {
        qCritical() << message;
        std::exit(1);
    }
}
}

int main(int argc, char *argv[]) {
    QTemporaryDir storage;
    require(storage.isValid(), "cannot create isolated storage");
    qputenv("XDG_CONFIG_HOME", storage.path().toUtf8());
    qputenv("XDG_DATA_HOME", storage.path().toUtf8());
    Q_INIT_RESOURCE(qtermwidget);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("qshell-quick-button-focus-test");
    QCoreApplication::setApplicationName("qshell-quick-button-focus-test");

    auto *config = ConfigManager::instance();
    ButtonGroup group;
    group.id = "focus-test-group";
    group.name = "Focus test";
    config->addButtonGroup(group);
    QuickButton button;
    button.id = "focus-test-button";
    button.groupId = group.id;
    button.name = "Quick command";
    button.command = "echo quick-button-focus-test\\r";
    config->addQuickButton(button);

    MainWindow window;
    auto *bar = window.findChild<CommandButtonBar *>();
    require(bar != nullptr, "missing quick button bar");
    bar->show();
    window.show();
    window.activateWindow();
    QApplication::processEvents();
    auto *quickButton = bar->findChild<QPushButton *>();
    require(quickButton != nullptr, "missing quick button");
    quickButton->click(); // No active session must also be safe.

    for (int i = 0; i < 2; ++i) {
        SessionData session;
        session.id = QString("focus-test-session-%1").arg(i);
        session.name = session.id;
        session.protocolType = ProtocolType::LocalShell;
        config->addSession(session);
        require(window.openSessionById(session.id), "cannot open terminal");
        auto *terminal = window.getCurrentSession();
        QApplication::processEvents();
        quickButton->setFocus(Qt::MouseFocusReason);
        require(quickButton->hasFocus(), "button did not receive focus before click");
        quickButton->click();
        QApplication::processEvents();
        require(terminal->hasFocus(), "quick button did not return focus to the active terminal");

        auto *command = window.findChild<CommandWindow *>();
        auto *editor = qobject_cast<QTextEdit *>(command->widget());
        editor->setFocus();
        command->commandSend("echo command-editor-focus-test\r");
        QApplication::processEvents();
        require(editor->hasFocus(), "command editor lost focus after sending a command");
    }
    for (auto *terminal : window.findChildren<BaseTerminal *>()) {
        terminal->disconnect();
    }
    qInfo() << "Quick button and command editor focus checks passed";
    return 0;
}
