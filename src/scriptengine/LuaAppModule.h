#pragma once

#include <QFile>
#include <functional>
#include <memory>
#include <sol/sol.hpp>

class LuaTimerModule;
class MainWindow;

class LuaAppModule {
public:
    LuaAppModule(MainWindow *mainWindow, LuaTimerModule &timerModule);

    void registerAPIs(sol::table &qshell, const std::function<void()> &requestStop);

    // 关闭日志文件（脚本结束后调用）
    void closeLog();

private:
    MainWindow *mainWindow_;
    LuaTimerModule &timerModule_;
    std::unique_ptr<QFile> logFile_;
};
