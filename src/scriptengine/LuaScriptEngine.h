// LuaScriptEngine.h
#pragma once
#include "LuaAppModule.h"
#include "LuaHttpModule.h"
#include "LuaScreenModule.h"
#include "LuaSerialModule.h"
#include "LuaSessionModule.h"
#include "LuaTimerModule.h"
#include "LuaZmodemModule.h"
#include <QObject>
#include <QString>
#include <QStringList>
#include <atomic>
#include <sol/sol.hpp>

class MainWindow;

class LuaScriptEngine : public QObject {
    Q_OBJECT
public:
    explicit LuaScriptEngine(MainWindow *window);

    bool executeScript(const QString &scriptPath, const QStringList &scriptArgs = {});
    bool executeCode(const QString &code);
    bool isRunning();
    static void stopScript();

signals:
    void scriptError(const QString &error);
    void scriptFinished();

private:
    void registerAPIs();

    sol::state lua_;
    MainWindow *mainWindow_ = nullptr;
    std::atomic<bool> running_{false};

    LuaTimerModule timerModule_;
    LuaAppModule appModule_;
    LuaScreenModule screenModule_;
    LuaSessionModule sessionModule_;
    LuaZmodemModule zmodemModule_;
    LuaHttpModule httpModule_;
    LuaSerialModule serialModule_;
};
