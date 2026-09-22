#pragma once

#include <QRegularExpression>
#include <QStringList>
#include <functional>
#include <sol/sol.hpp>

class LuaTimerModule;
class MainWindow;

class LuaScreenModule {
public:
    LuaScreenModule(MainWindow *mainWindow, LuaTimerModule &timerModule);

    void registerAPIs(sol::table &qshell, const std::function<bool()> &shouldStop);

private:
    bool waitForStrings(const QStringList &strings, int timeoutSeconds);
    void onDisplayOutput(const QString &line);

    MainWindow *mainWindow_;
    LuaTimerModule &timerModule_;
    std::function<bool()> shouldStop_;

    // waitForRegexp 相关状态
    QRegularExpression waitForRegexp_;
    bool findWaitForRegexp_ = false;
    QString lastRegexpMatch_;
};
