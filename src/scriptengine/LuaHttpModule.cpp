#include "LuaHttpModule.h"

#include "LuaTimerModule.h"

#include <QEventLoop>
#include <QMap>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <utility>

LuaHttpModule::LuaHttpModule(LuaTimerModule &timerModule)
    : timerModule_(timerModule) {}

// ========== qshell.http 模块 ==========
void LuaHttpModule::registerAPIs(sol::state &lua, sol::table &qshell, const std::function<bool()> &shouldStop)
{
    shouldStop_ = shouldStop;
    lua_ = &lua;

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
        sol::table mergedOptions = lua_->create_table();
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
sol::table LuaHttpModule::performHttpRequest(const std::string& method,
                                                const std::string& url,
                                                const std::string& body,
                                                sol::optional<sol::table> options)
{
    sol::table result = lua_->create_table();

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
        result["headers"] = lua_->create_table();
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
        if (shouldStop_()) {
            interrupted = true;
            reply->abort();
            loop.quit();
        }
        // 处理脚本定时器
        timerModule_.processTimers();
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
        result["headers"] = lua_->create_table();
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
    sol::table headersTable = lua_->create_table();
    for (auto it = responseHeaders.begin(); it != responseHeaders.end(); ++it) {
        headersTable[it.key().toStdString()] = it.value().toStdString();
    }
    result["headers"] = headersTable;

    return result;
}
