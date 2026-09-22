#include "LuaSessionModule.h"

#include "ui/MainWindow.h"
#include "ui/terminal/BaseTerminal.h"

LuaSessionModule::LuaSessionModule(MainWindow *mainWindow)
    : mainWindow_(mainWindow) {}

void LuaSessionModule::registerAPIs(sol::table &qshell) {
    sol::table session = qshell.create_named("session");

    session.set_function("open", [this](const std::string& sessionName) -> bool {
        bool ok = false;
        QMetaObject::invokeMethod(mainWindow_, [this, sessionName, &ok]() {
            ok = mainWindow_->openSessionByName(sessionName.data());
        }, Qt::BlockingQueuedConnection);
        return ok;
    });

    session.set_function("tabName", [this]() -> std::string {
        auto currentSession = mainWindow_->getCurrentSession();
        if (currentSession != nullptr) {
            return currentSession->getSessionName().toStdString();
        }
        return "";
    });

    session.set_function("nextTab", [this]() {
        QMetaObject::invokeMethod(mainWindow_, [this]() {
            mainWindow_->nextTab();
        }, Qt::BlockingQueuedConnection);
    });

    session.set_function("switchToTab", [this](const std::string& tabName) -> bool {
        bool ok = false;
        QMetaObject::invokeMethod(mainWindow_, [this, tabName, &ok]() {
            ok = mainWindow_->switchToTab(tabName.data());
        }, Qt::BlockingQueuedConnection);
        return ok;
    });

    session.set_function("connect", [this]() {
        QMetaObject::invokeMethod(mainWindow_, "onConnectAction",
            Qt::BlockingQueuedConnection);
    });

    session.set_function("disconnect", [this]() {
        QMetaObject::invokeMethod(mainWindow_, "onDisconnectAction",
            Qt::BlockingQueuedConnection);
    });
}
