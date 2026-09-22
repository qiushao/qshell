// LuaScriptEngine.cpp
#include "LuaScriptEngine.h"
#include "ui/MainWindow.h"
#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>
#include <functional>

// 全局停止标志（线程安全）
std::atomic<bool> gShouldStop{false};

// 钩子函数：每执行一定数量的指令就检查是否需要停止
void interruptHook(lua_State* L, lua_Debug* ar) {
    if (gShouldStop.load()) {
        luaL_error(L, "Script execution interrupted by user");
    }
}

LuaScriptEngine::LuaScriptEngine(MainWindow* mainWindow)
    : QObject(mainWindow), mainWindow_(mainWindow),
      appModule_(mainWindow, timerModule_),
      screenModule_(mainWindow, timerModule_),
      sessionModule_(mainWindow),
      zmodemModule_(mainWindow),
      httpModule_(timerModule_)
{
    lua_.open_libraries(sol::lib::base, sol::lib::string,
                         sol::lib::table, sol::lib::math,
                         sol::lib::os, sol::lib::io, sol::lib::package);

    QString scriptDir = QCoreApplication::applicationDirPath() + "/scripts";
    std::string currentPath = lua_["package"]["path"];
    std::string newPath = currentPath + ";"
            + scriptDir.toStdString() + "/?.lua;"
            + scriptDir.toStdString() + "/?/init.lua";

    lua_["package"]["path"] = newPath;

    registerAPIs();
    lua_sethook(lua_.lua_state(), interruptHook, LUA_MASKCOUNT, 1000);
}

void LuaScriptEngine::registerAPIs()
{
    sol::table qshell = lua_.create_named_table("qshell");
    const std::function<bool()> shouldStop = [] { return gShouldStop.load(); };
    const std::function<void()> requestStop = [] { gShouldStop = true; };

    appModule_.registerAPIs(qshell, requestStop);
    screenModule_.registerAPIs(qshell, shouldStop);
    sessionModule_.registerAPIs(qshell);
    zmodemModule_.registerAPIs(qshell);
    timerModule_.registerAPIs(qshell, shouldStop);
    httpModule_.registerAPIs(lua_, qshell, shouldStop);
    serialModule_.registerAPIs(lua_, qshell, shouldStop);
}

bool LuaScriptEngine::executeScript(const QString& scriptPath, const QStringList& scriptArgs)
{
#ifdef Q_OS_WIN
    // Windows 下检测路径是否包含非 ASCII 字符（中文等）
    static const QRegularExpression nonAsciiRe(QStringLiteral("[^\\x00-\\x7F]"));
    if (scriptPath.contains(nonAsciiRe)) {
        emit scriptError(tr("脚本路径包含中文或其他非 ASCII 字符，Windows 下暂不支持。\n"
                            "请将脚本移动到纯英文路径下再运行。\n\n"
                            "当前路径: %1").arg(scriptPath));
        return false;
    }
#endif

    QFileInfo fileInfo(scriptPath);
    QString scriptDir = fileInfo.absolutePath();

    // 动态添加脚本所在目录到 package.path
    std::string currentPath = lua_["package"]["path"];
    std::string dirPath = scriptDir.toStdString();

    // 替换反斜杠（Windows 兼容）
    std::replace(dirPath.begin(), dirPath.end(), '\\', '/');

    lua_["package"]["path"] = dirPath + "/?.lua;"
                           + dirPath + "/?/init.lua;"
                           + currentPath;

    // 与 lua 命令行兼容：arg[0] 为脚本路径，arg[1..n] 为参数
    sol::table argTable = lua_.create_table();
    argTable[0] = scriptPath.toStdString();
    for (int i = 0; i < scriptArgs.size(); ++i) {
        argTable[i + 1] = scriptArgs.at(i).toStdString();
    }
    lua_["arg"] = argTable;

    running_ = true;
    gShouldStop = false;  // 重置停止标志
    timerModule_.reset();  // 清理之前的定时器

    try {
        auto result = lua_.script_file(scriptPath.toStdString());
        appModule_.closeLog();
        serialModule_.closeAll();
        running_ = false;
        emit scriptFinished();
        return result.valid();
    } catch (const sol::error& e) {
        appModule_.closeLog();
        serialModule_.closeAll();
        running_ = false;
        emit scriptError(QString::fromStdString(e.what()));
        return false;
    }
}

bool LuaScriptEngine::executeCode(const QString& code)
{
    running_ = true;
    gShouldStop = false;  // 重置停止标志
    timerModule_.reset();  // 清理之前的定时器

    try {
        auto result = lua_.script(code.toStdString());
        appModule_.closeLog();
        serialModule_.closeAll();
        running_ = false;
        emit scriptFinished();
        return result.valid();
    } catch (const sol::error& e) {
        appModule_.closeLog();
        serialModule_.closeAll();
        running_ = false;
        emit scriptError(QString::fromStdString(e.what()));
        return false;
    }
}

bool LuaScriptEngine::isRunning() {
    return running_;
}

void LuaScriptEngine::stopScript()
{
    qDebug() << "stopScript";
    gShouldStop = true;
}
