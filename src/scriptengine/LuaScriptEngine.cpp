// LuaScriptEngine.cpp
#include "LuaScriptEngine.h"
#include <QThread>
#include <QEventLoop>
#include <QTimer>
#include <QRegularExpression>
#include "ui/MainWindow.h"
#include "ui/terminal/BaseTerminal.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>
#include <thread>
#include <utility>

// 全局停止标志（线程安全）
std::atomic<bool> gShouldStop{false};

// 钩子函数：每执行一定数量的指令就检查是否需要停止
void interruptHook(lua_State* L, lua_Debug* ar) {
    if (gShouldStop.load()) {
        luaL_error(L, "Script execution interrupted by user");
    }
}

LuaScriptEngine::LuaScriptEngine(MainWindow* mainWindow)
    : QObject(mainWindow), mainWindow_(mainWindow)
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

    registerAppModule(qshell);
    registerScreenModule(qshell);
    registerSessionModule(qshell);
    registerZmodemModule(qshell);
    registerTimerModule(qshell);
    registerHttpModule(qshell);
}

// 可中断的 sleep，同时处理定时器
void LuaScriptEngine::interruptibleSleep(int milliseconds)
{
    auto endTime = std::chrono::steady_clock::now() 
                   + std::chrono::milliseconds(milliseconds);

    while (std::chrono::steady_clock::now() < endTime) {
        if (gShouldStop.load()) {
            throw std::runtime_error("interrupted during sleep");
        }
        
        // 处理定时器回调
        processTimers();
        
        // 计算剩余等待时间，最多等待 50ms
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - std::chrono::steady_clock::now()).count();
        auto sleepTime = std::min<long>(50L, remaining);
        if (sleepTime > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepTime));
        }
    }
    
    // 最后再处理一次定时器
    processTimers();
}

// 处理所有到期的定时器
void LuaScriptEngine::processTimers()
{
    std::lock_guard<std::mutex> lock(timersMutex_);
    
    auto now = std::chrono::steady_clock::now();
    
    for (auto& timer : timers_) {
        if (!timer.active) continue;
        
        if (now >= timer.nextTrigger) {
            // 执行回调
            try {
                if (timer.callback.valid()) {
                    timer.callback();
                }
            } catch (const sol::error& e) {
                qWarning() << "Timer callback error:" << e.what();
            }
            
            if (timer.intervalMs > 0) {
                // 重复定时器：更新下次触发时间
                timer.nextTrigger = now + std::chrono::milliseconds(timer.intervalMs);
            } else {
                // 单次定时器：标记为非活动
                timer.active = false;
            }
        }
    }
    
    // 清理已完成的单次定时器
    timers_.erase(
        std::remove_if(timers_.begin(), timers_.end(),
            [](const TimerInfo& t) { return !t.active; }),
        timers_.end()
    );
}

// ========== qshell.timer 模块 ==========
void LuaScriptEngine::registerTimerModule(sol::table& qshell)
{
    sol::table timer = qshell.create_named("timer");

    // qshell.timer.setTimeout(callback, delayMs)
    // 创建单次定时器，返回 timerId
    // 示例: local id = qshell.timer.setTimeout(function() print("timeout!") end, 1000)
    timer.set_function("setTimeout", [this](sol::function callback, int delayMs) -> int {
        std::lock_guard<std::mutex> lock(timersMutex_);
        
        int id = nextTimerId_++;
        TimerInfo info;
        info.id = id;
        info.nextTrigger = std::chrono::steady_clock::now() 
                           + std::chrono::milliseconds(delayMs);
        info.intervalMs = 0;  // 单次
        info.callback = std::move(callback);
        info.active = true;
        
        timers_.push_back(std::move(info));
        return id;
    });

    // qshell.timer.setInterval(callback, intervalMs)
    // 创建重复定时器，返回 timerId
    // 示例: local id = qshell.timer.setInterval(function() print("tick") end, 500)
    timer.set_function("setInterval", [this](sol::function callback, int intervalMs) -> int {
        std::lock_guard<std::mutex> lock(timersMutex_);
        
        int id = nextTimerId_++;
        TimerInfo info;
        info.id = id;
        info.nextTrigger = std::chrono::steady_clock::now() 
                           + std::chrono::milliseconds(intervalMs);
        info.intervalMs = intervalMs;  // 重复间隔
        info.callback = std::move(callback);
        info.active = true;
        
        timers_.push_back(std::move(info));
        return id;
    });

    // qshell.timer.clear(timerId)
    // 取消指定定时器
    // 示例: qshell.timer.clear(id)
    timer.set_function("clear", [this](int timerId) -> bool {
        std::lock_guard<std::mutex> lock(timersMutex_);
        
        for (auto& timer : timers_) {
            if (timer.id == timerId) {
                timer.active = false;
                return true;
            }
        }
        return false;
    });

    // qshell.timer.clearAll()
    // 取消所有定时器
    timer.set_function("clearAll", [this]() {
        std::lock_guard<std::mutex> lock(timersMutex_);
        timers_.clear();
    });

    // qshell.timer.process()
    // 手动处理所有到期的定时器回调
    // 在长时间运行的循环中应定期调用此函数
    timer.set_function("process", [this]() {
        processTimers();
    });

    // qshell.timer.count()
    // 返回当前活动定时器数量
    timer.set_function("count", [this]() -> int {
        std::lock_guard<std::mutex> lock(timersMutex_);
        return static_cast<int>(timers_.size());
    });

    // qshell.timer.sleep(milliseconds)
    // 可中断的 sleep，同时处理定时器回调
    // 示例: qshell.timer.sleep(2000)  -- 睡眠2秒，期间定时器仍会触发
    timer.set_function("sleep", [this](int milliseconds) {
        interruptibleSleep(milliseconds);
    });
}

// ========== qshell 模块 ==========
void LuaScriptEngine::registerAppModule(sol::table& qshell) {
    qshell.set_function("showMessage", [this](const std::string& msg) {
        QString qmsg = QString::fromStdString(msg);
        QMetaObject::invokeMethod(mainWindow_, [qmsg]() {
            QMessageBox::information(nullptr, "Script Message", qmsg);
        }, Qt::BlockingQueuedConnection);
    });

    qshell.set_function("input", [this](const std::string& prompt,
                                         sol::optional<std::string> defaultValue,
                                         sol::optional<std::string> title) -> std::string {
        QString qprompt = QString::fromStdString(prompt);
        QString qdefault = defaultValue ? QString::fromStdString(defaultValue.value()) : QString();
        QString qtitle = title ? QString::fromStdString(title.value()) : "Script Input";
        QString result;
        bool ok = false;

        QMetaObject::invokeMethod(mainWindow_, [qtitle, qprompt, qdefault, &result, &ok]() {
            result = QInputDialog::getText(nullptr, qtitle, qprompt,
                                           QLineEdit::Normal, qdefault, &ok);
        }, Qt::BlockingQueuedConnection);

        if (!ok) {
            return "";
        }
        return result.toStdString();
    });

    qshell.set_function("log", [](const std::string& msg) {
        qDebug() << QString::fromStdString(msg);
    });

    // 修改 sleep 使用可中断版本并处理定时器
    qshell.set_function("sleep", [this](double seconds) {
        interruptibleSleep(static_cast<int>(seconds * 1000));
    });

    qshell.set_function("getVersionStr", []() {
        return QCoreApplication::applicationVersion().toStdString();
    });

    qshell.set_function("exit", [this](sol::optional<int> code) {
        const int exitCode = code.value_or(0);
        gShouldStop = true;
        QMetaObject::invokeMethod(mainWindow_, [exitCode]() {
            QCoreApplication::exit(exitCode);
        }, Qt::QueuedConnection);
    });
}

// ========== qshell.screen 模块 ==========
void LuaScriptEngine::registerScreenModule(sol::table& qshell)
{
    sol::table screen = qshell.create_named("screen");

    screen.set_function("sendText", [this](const std::string& command) {
        auto qstr = QString::fromStdString(command);
        QMetaObject::invokeMethod(mainWindow_, "onCommandSend",
            Qt::BlockingQueuedConnection,
            Q_ARG(QString, qstr));
    });

    screen.set_function("sendKey", [this](const std::string& keyName) {
        QString qkey = QString::fromStdString(keyName);
        QMetaObject::invokeMethod(mainWindow_, "onSendKey",
            Qt::BlockingQueuedConnection,
            Q_ARG(QString, qkey));
    });

    screen.set_function("getScreenText", [this]() -> std::string {
        QString text;
        QMetaObject::invokeMethod(mainWindow_, "getScreenText",
            Qt::BlockingQueuedConnection,
            Q_RETURN_ARG(QString, text));
        return text.toStdString();
    });

    screen.set_function("getLastLine", [this]() -> std::string {
        QString text;
        QMetaObject::invokeMethod(mainWindow_, "getLastLine",
                                  Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(QString, text));
        return text.toStdString();
    });

    screen.set_function("containString", [this](const std::string& str) -> bool {
        QString screenText;
        QMetaObject::invokeMethod(mainWindow_, "getScreenText",
                                  Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(QString, screenText));
        return screenText.contains(QString::fromStdString(str));
    });


    screen.set_function("clear", [this]() {
        QMetaObject::invokeMethod(mainWindow_, "onClearScreenAction",
            Qt::BlockingQueuedConnection);
    });

    // 修改 waitForString 以支持定时器
    screen.set_function("waitForString", [this](const std::string& str, int timeoutSeconds) -> bool {
        isWaitForString_ = true;
        waitForString_ = QString::fromStdString(str);
        findWaitForString_ = false;
        auto currentSession = mainWindow_->getCurrentSession();
        QObject::connect(currentSession, &QTermWidget::onNewLine, this, &LuaScriptEngine::onDisplayOutput);
        
        auto endTime = std::chrono::steady_clock::now()
                          + std::chrono::milliseconds(timeoutSeconds * 1000);

        int pollCounter = 0;
        constexpr int pollInterval = 4;  // 每4次循环检查一次屏幕内容（约200ms）
        while (std::chrono::steady_clock::now() < endTime) {
            if (gShouldStop.load()) {
                QObject::disconnect(currentSession, &QTermWidget::onNewLine, this, &LuaScriptEngine::onDisplayOutput);
                throw std::runtime_error("interrupted during waitForString");
            }

            // 处理定时器
            processTimers();

            if (findWaitForString_) {
                isWaitForString_ = false;
                QObject::disconnect(currentSession, &QTermWidget::onNewLine, this, &LuaScriptEngine::onDisplayOutput);
                return true;
            }

            // 定期轮询屏幕内容（检查最后一行）
            if (++pollCounter >= pollInterval) {
                pollCounter = 0;
                auto lastLine = mainWindow_->getLastLine();
                if (lastLine.contains(waitForString_)) {
                    findWaitForString_ = true;
                    isWaitForString_ = false;
                    QObject::disconnect(currentSession, &QTermWidget::onNewLine, this, &LuaScriptEngine::onDisplayOutput);
                    return true;
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        isWaitForString_ = false;
        QObject::disconnect(currentSession, &QTermWidget::onNewLine, this, &LuaScriptEngine::onDisplayOutput);
        return false;
    });

    // 修改 waitForRegexp 以支持定时器
    screen.set_function("waitForRegexp", [this](const std::string& pattern, int timeoutSeconds) -> bool {
        isWaitForRegexp_ = true;
        waitForRegexp_ = QRegularExpression(QString::fromStdString(pattern));
        findWaitForRegexp_ = false;
        lastRegexpMatch_.clear();

        if (!waitForRegexp_.isValid()) {
            qWarning() << "Invalid regexp pattern:" << waitForRegexp_.errorString();
            isWaitForRegexp_ = false;
            return false;
        }

        auto currentSession = mainWindow_->getCurrentSession();
        QObject::connect(currentSession, &QTermWidget::onNewLine,
                         this, &LuaScriptEngine::onDisplayOutput);

        auto endTime = std::chrono::steady_clock::now()
                          + std::chrono::milliseconds(timeoutSeconds * 1000);

        int pollCounter = 0;
        constexpr int pollInterval = 4;  // 每4次循环检查一次屏幕内容（约200ms）
        while (std::chrono::steady_clock::now() < endTime) {
            if (gShouldStop.load()) {
                isWaitForRegexp_ = false;
                QObject::disconnect(currentSession, &QTermWidget::onNewLine,
                                   this, &LuaScriptEngine::onDisplayOutput);
                throw std::runtime_error("interrupted during waitForRegexp");
            }

            // 处理定时器
            processTimers();

            if (findWaitForRegexp_) {
                isWaitForRegexp_ = false;
                QObject::disconnect(currentSession, &QTermWidget::onNewLine,
                                   this, &LuaScriptEngine::onDisplayOutput);
                return true;
            }

            // 定期轮询屏幕内容（检查最后一行）
            if (++pollCounter >= pollInterval) {
                pollCounter = 0;
                auto lastLine = mainWindow_->getLastLine();
                QRegularExpressionMatch match = waitForRegexp_.match(lastLine);
                if (match.hasMatch()) {
                    findWaitForRegexp_ = true;
                    lastRegexpMatch_ = match.captured(0);
                    isWaitForRegexp_ = false;
                    QObject::disconnect(currentSession, &QTermWidget::onNewLine,
                                        this, &LuaScriptEngine::onDisplayOutput);
                    return true;
                }

            }

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        isWaitForRegexp_ = false;
        QObject::disconnect(currentSession, &QTermWidget::onNewLine,
                           this, &LuaScriptEngine::onDisplayOutput);
        return false;
    });

    screen.set_function("getLastMatch", [this]() -> std::string {
        return lastRegexpMatch_.toStdString();
    });
}

void LuaScriptEngine::registerSessionModule(sol::table &qshell) {
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

void LuaScriptEngine::registerZmodemModule(
        sol::table &qshell) {
    sol::table zmodem = qshell.create_named("zmodem");

    zmodem.set_function(
            "upload",
            [this](const sol::object &pathOrPaths) -> bool {
                QStringList filePaths;
                if (pathOrPaths.is<std::string>()) {
                    filePaths.append(QString::fromStdString(
                            pathOrPaths.as<std::string>()));
                } else if (pathOrPaths.is<sol::table>()) {
                    const sol::table paths =
                            pathOrPaths.as<sol::table>();
                    for (const auto &entry: paths) {
                        if (!entry.second.is<std::string>()) {
                            return false;
                        }
                        filePaths.append(QString::fromStdString(
                                entry.second.as<std::string>()));
                    }
                } else {
                    return false;
                }

                bool prepared = false;
                QMetaObject::invokeMethod(
                        mainWindow_,
                        [this, filePaths, &prepared]() {
                            prepared =
                                    mainWindow_->prepareZmodemUpload(
                                            filePaths);
                        },
                        Qt::BlockingQueuedConnection);
                return prepared;
            });

    zmodem.set_function(
            "download",
            [this](const std::string &directoryPath) -> bool {
                const QString path =
                        QString::fromStdString(directoryPath);
                bool prepared = false;
                QMetaObject::invokeMethod(
                        mainWindow_,
                        [this, path, &prepared]() {
                            prepared =
                                    mainWindow_->prepareZmodemDownload(
                                            path);
                        },
                        Qt::BlockingQueuedConnection);
                return prepared;
            });
}

void LuaScriptEngine::registerHttpModule(sol::table& qshell)
{
    sol::table http = qshell.create_named("http");

    // qshell.http.get(url, [options])
    // options: { timeout = 30000, headers = { ["Content-Type"] = "application/json" } }
    // 返回: { status = 200, body = "...", headers = { ... }, error = nil }
    // 示例: local resp = qshell.http.get("https://api.example.com/data")
    http.set_function("get", [this](const std::string& url,
                                     sol::optional<sol::table> options) -> sol::table {
        return performHttpRequest("GET", url, "", std::move(options));
    });

    // qshell.http.post(url, body, [options])
    // body: 请求体字符串（可以是 JSON 字符串）
    // options: { timeout = 30000, headers = { ["Content-Type"] = "application/json" } }
    // 返回: { status = 200, body = "...", headers = { ... }, error = nil }
    // 示例: local resp = qshell.http.post("https://api.example.com/data", '{"key":"value"}')
    http.set_function("post", [this](const std::string& url,
                                      const std::string& body,
                                      sol::optional<sol::table> options) -> sol::table {
        return performHttpRequest("POST", url, body, std::move(options));
    });

    // qshell.http.postForm(url, formData, [options])
    // formData: Lua table，键值对形式 { username = "test", password = "123" }
    // 自动设置 Content-Type 为 application/x-www-form-urlencoded
    // 示例: local resp = qshell.http.postForm("https://example.com/login", { user = "admin", pass = "123" })
    http.set_function("postForm", [this](const std::string& url, const sol::table &formData,
                                          sol::optional<sol::table> options) -> sol::table {
        // 构建 URL 编码的表单数据
        QUrlQuery query;
        for (auto& pair : formData) {
            if (pair.first.is<std::string>() && pair.second.is<std::string>()) {
                query.addQueryItem(
                    QString::fromStdString(pair.first.as<std::string>()),
                    QString::fromStdString(pair.second.as<std::string>())
                );
            }
        }

        std::string body = query.toString(QUrl::FullyEncoded).toStdString();

        // 创建带有正确 Content-Type 的 options
        sol::table mergedOptions = lua_.create_table();
        mergedOptions["contentType"] = "application/x-www-form-urlencoded";

        if (options.has_value()) {
            // 复制用户的 options
            for (auto& pair : options.value()) {
                mergedOptions[pair.first] = pair.second;
            }
            // 但强制使用表单 Content-Type
            mergedOptions["contentType"] = "application/x-www-form-urlencoded";
        }

        return performHttpRequest("POST", url, body, mergedOptions);
    });
}

// ========== HTTP 请求核心实现 ==========
sol::table LuaScriptEngine::performHttpRequest(const std::string& method,
                                                const std::string& url,
                                                const std::string& body,
                                                sol::optional<sol::table> options)
{
    sol::table result = lua_.create_table();

    // 解析选项
    int timeoutMs = 30000;  // 默认 30 秒超时
    QString contentType = "application/json";
    QMap<QString, QString> customHeaders;

    if (options.has_value()) {
        sol::table opts = options.value();

        // 超时设置
        if (opts["timeout"].valid() && opts["timeout"].is<int>()) {
            timeoutMs = opts["timeout"].get<int>();
        }

        // Content-Type
        if (opts["contentType"].valid() && opts["contentType"].is<std::string>()) {
            contentType = QString::fromStdString(opts["contentType"].get<std::string>());
        }

        // 自定义请求头
        if (opts["headers"].valid() && opts["headers"].is<sol::table>()) {
            sol::table headers = opts["headers"];
            for (auto& pair : headers) {
                if (pair.first.is<std::string>() && pair.second.is<std::string>()) {
                    customHeaders[QString::fromStdString(pair.first.as<std::string>())] =
                        QString::fromStdString(pair.second.as<std::string>());
                }
            }
        }
    }

    // 创建网络管理器和请求
    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(QString::fromStdString(url)));

    // 设置请求头
    request.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    // request.setHeader(QNetworkRequest::UserAgentHeader, "QShell/1.0");

    for (auto it = customHeaders.begin(); it != customHeaders.end(); ++it) {
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }

    // 发送请求
    QNetworkReply* reply = nullptr;
    if (method == "GET") {
        reply = manager.get(request);
    } else if (method == "POST") {
        reply = manager.post(request, QByteArray::fromStdString(body));
    }

    if (!reply) {
        result["status"] = -1;
        result["body"] = "";
        result["error"] = "Failed to create network request";
        result["headers"] = lua_.create_table();
        return result;
    }

    // 使用 QEventLoop 实现同步等待
    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);

    bool timedOut = false;
    bool interrupted = false;

    // 请求完成时退出循环
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    // 超时处理
    QObject::connect(&timeoutTimer, &QTimer::timeout, [&]() {
        timedOut = true;
        reply->abort();
        loop.quit();
    });

    // 定期检查中断标志和处理定时器
    QTimer checkTimer;
    QObject::connect(&checkTimer, &QTimer::timeout, [&]() {
        if (gShouldStop.load()) {
            interrupted = true;
            reply->abort();
            loop.quit();
        }
        // 处理脚本定时器
        processTimers();
    });

    timeoutTimer.start(timeoutMs);
    checkTimer.start(50);

    // 阻塞等待请求完成
    loop.exec();

    checkTimer.stop();
    timeoutTimer.stop();

    // 如果被中断
    if (interrupted) {
        reply->deleteLater();
        result["status"] = -1;
        result["body"] = "";
        result["error"] = "Request interrupted by user";
        result["headers"] = lua_.create_table();
        return result;
    }

    // 处理响应
    int statusCode = 0;
    QByteArray responseBody;
    QMap<QString, QString> responseHeaders;
    QString errorString;

    if (reply->error() == QNetworkReply::NoError) {
        statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        responseBody = reply->readAll();

        // 获取响应头
        for (const auto& header : reply->rawHeaderPairs()) {
            responseHeaders[QString::fromUtf8(header.first)] = QString::fromUtf8(header.second);
        }
    } else {
        statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (statusCode == 0) {
            statusCode = -1;
        }
        responseBody = reply->readAll();

        if (timedOut) {
            errorString = "Request timeout";
        } else {
            errorString = reply->errorString();
        }
    }

    reply->deleteLater();

    // 构建返回结果
    result["status"] = statusCode;
    result["body"] = responseBody.toStdString();

    if (!errorString.isEmpty()) {
        result["error"] = errorString.toStdString();
    } else {
        result["error"] = "";
    }

    // 转换响应头为 Lua table
    sol::table headersTable = lua_.create_table();
    for (auto it = responseHeaders.begin(); it != responseHeaders.end(); ++it) {
        headersTable[it.key().toStdString()] = it.value().toStdString();
    }
    result["headers"] = headersTable;

    return result;
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
    
    // 清理之前的定时器
    {
        std::lock_guard<std::mutex> lock(timersMutex_);
        timers_.clear();
        nextTimerId_ = 1;
    }
    
    try {
        auto result = lua_.script_file(scriptPath.toStdString());
        running_ = false;
        emit scriptFinished();
        return result.valid();
    } catch (const sol::error& e) {
        running_ = false;
        emit scriptError(QString::fromStdString(e.what()));
        return false;
    }
}

bool LuaScriptEngine::executeCode(const QString& code)
{
    running_ = true;
    gShouldStop = false;  // 重置停止标志
    
    // 清理之前的定时器
    {
        std::lock_guard<std::mutex> lock(timersMutex_);
        timers_.clear();
        nextTimerId_ = 1;
    }
    
    try {
        auto result = lua_.script(code.toStdString());
        running_ = false;
        emit scriptFinished();
        return result.valid();
    } catch (const sol::error& e) {
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

void LuaScriptEngine::onDisplayOutput(const QString &line) {
    if (isWaitForString_) {
        if (line.contains(waitForString_)) {
            findWaitForString_ = true;
        }
    }

    if (isWaitForRegexp_) {
        QRegularExpressionMatch match = waitForRegexp_.match(line);
        if (match.hasMatch()) {
            findWaitForRegexp_ = true;
            lastRegexpMatch_ = match.captured(0);
        }
    }
}
