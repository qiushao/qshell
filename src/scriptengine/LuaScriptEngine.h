// LuaScriptEngine.h
#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QRegularExpression>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <sol/sol.hpp>
#include <chrono>
#include <vector>
#include <mutex>

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
    void registerAppModule(sol::table &qshell);
    void registerScreenModule(sol::table &qshell);
    void registerSessionModule(sol::table &qshell);
    void registerZmodemModule(sol::table &qshell);
    void registerTimerModule(sol::table &qshell);
    void registerHttpModule(sol::table &qshell);
    void onDisplayOutput(const QString &line);

    // 定时器处理
    void processTimers();
    void interruptibleSleep(int milliseconds);

    // HTTP 请求辅助方法
    sol::table performHttpRequest(const std::string& method,
                                   const std::string& url,
                                   const std::string& body,
                                   sol::optional<sol::table> options);

    sol::state lua_;
    MainWindow *mainWindow_ = nullptr;
    std::atomic<bool> running_{false};
    bool isWaitForString_ = false;
    bool findWaitForString_ = false;
    QString waitForString_;

    // waitForRegexp 相关变量
    bool isWaitForRegexp_ = false;
    QRegularExpression waitForRegexp_;
    bool findWaitForRegexp_ = false;
    QString lastRegexpMatch_;

    // Timer 模块相关
    struct TimerInfo {
        int id;
        std::chrono::steady_clock::time_point nextTrigger;
        int intervalMs;  // 0 = 单次定时器, >0 = 重复定时器
        sol::function callback;
        bool active;
    };

    std::vector<TimerInfo> timers_;
    std::mutex timersMutex_;
    int nextTimerId_ = 1;
};
