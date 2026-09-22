#include "LuaScreenModule.h"

#include "LuaTimerModule.h"
#include "ui/MainWindow.h"
#include "ui/terminal/BaseTerminal.h"

#include <QScopeGuard>
#include <algorithm>
#include <iterator>
#include <thread>

LuaScreenModule::LuaScreenModule(MainWindow *mainWindow, LuaTimerModule &timerModule)
    : mainWindow_(mainWindow), timerModule_(timerModule) {}

void LuaScreenModule::onDisplayOutput(const QString &line) {
    QRegularExpressionMatch match = waitForRegexp_.match(line);
    if (match.hasMatch()) {
        findWaitForRegexp_ = true;
        lastRegexpMatch_ = match.captured(0);
    }
}

// ========== qshell.screen 模块 ==========
void LuaScreenModule::registerAPIs(sol::table &qshell, const std::function<bool()> &shouldStop)
{
    shouldStop_ = shouldStop;

    sol::table screen = qshell.create_named("screen");

    screen.set_function("sendText", [this](const std::string& command) {
        auto qstr = QString::fromStdString(command);
        QMetaObject::invokeMethod(mainWindow_, "onCommandSend",
            Qt::BlockingQueuedConnection,
            Q_ARG(QString, qstr));
    });

    screen.set_function("sendBinary", [this](const sol::object& data) -> bool {
        QByteArray bytes;
        auto appendByte = [&bytes](const sol::object& value) -> bool {
            if (value.get_type() != sol::type::number) {
                return false;
            }
            const double number = value.as<double>();
            if (!(number >= 0 && number <= 255) || number != static_cast<int>(number)) {
                return false;
            }
            bytes.append(static_cast<char>(static_cast<unsigned char>(number)));
            return true;
        };
        if (data.is<sol::table>()) {
            const sol::table values = data.as<sol::table>();
            const qsizetype size = std::distance(values.begin(), values.end());
            for (qsizetype i = 0; i < size; ++i) {
                if (!appendByte(values.raw_get<sol::object>(i + 1))) {
                    return false;
                }
            }
        } else if (!appendByte(data)) {
            return false;
        }
        bool sent = false;
        QMetaObject::invokeMethod(mainWindow_, [this, bytes, &sent]() {
            sent = mainWindow_->sendBinaryToCurrent(bytes);
        }, Qt::BlockingQueuedConnection);
        return sent;
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

    screen.set_function("waitForString", [this](const std::string& str, int timeoutSeconds) -> bool {
        return waitForStrings({QString::fromStdString(str)}, timeoutSeconds);
    });

    screen.set_function("waitForStrings", [this](const sol::table& strings, int timeoutSeconds) -> bool {
        QStringList targets;
        const qsizetype size = std::distance(strings.begin(), strings.end());
        for (qsizetype i = 0; i < size; ++i) {
            const auto value = strings.raw_get<sol::object>(i + 1);
            if (value.get_type() != sol::type::string) {
                throw std::runtime_error("waitForStrings: expected an array of strings");
            }
            targets.append(QString::fromStdString(value.as<std::string>()));
        }
        return waitForStrings(targets, timeoutSeconds);
    });

    // waitForRegexp 支持定时器
    screen.set_function("waitForRegexp", [this](const std::string& pattern, int timeoutSeconds) -> bool {
        waitForRegexp_ = QRegularExpression(QString::fromStdString(pattern));
        findWaitForRegexp_ = false;
        lastRegexpMatch_.clear();

        if (!waitForRegexp_.isValid()) {
            qWarning() << "Invalid regexp pattern:" << waitForRegexp_.errorString();
            return false;
        }

        // 输出信号在 UI 线程触发，等待循环在脚本线程运行。
        QMetaObject::Connection connection;
        auto currentSession = mainWindow_->getCurrentSession();
        if (currentSession) {
            connection = QObject::connect(currentSession, &QTermWidget::onNewLine, mainWindow_,
                                          [this](const QString& line) { onDisplayOutput(line); });
        }
        const auto cleanup = qScopeGuard([connection]() { QObject::disconnect(connection); });

        auto endTime = std::chrono::steady_clock::now()
                          + std::chrono::milliseconds(timeoutSeconds * 1000);

        int pollCounter = 0;
        constexpr int pollInterval = 4;  // 每4次循环检查一次屏幕内容（约200ms）
        while (std::chrono::steady_clock::now() < endTime) {
            if (shouldStop_()) {
                throw std::runtime_error("interrupted during waitForRegexp");
            }

            // 处理定时器
            timerModule_.processTimers();

            if (findWaitForRegexp_) {
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
                    return true;
                }

            }

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        return false;
    });

    screen.set_function("getLastMatch", [this]() -> std::string {
        return lastRegexpMatch_.toStdString();
    });
}

bool LuaScreenModule::waitForStrings(const QStringList& strings, int timeoutSeconds) {
    if (strings.isEmpty() || timeoutSeconds <= 0) {
        return false;
    }

    // 输出信号在 UI 线程触发，等待循环在脚本线程运行。
    auto found = std::make_shared<std::atomic<bool>>(false);
    auto match = [strings, found](const QString& line) {
        if (std::any_of(strings.cbegin(), strings.cend(), [&line](const QString& str) {
                return line.contains(str);
            })) {
            found->store(true);
        }
    };
    QMetaObject::Connection connection;
    QMetaObject::invokeMethod(mainWindow_, [this, &connection, match]() {
        auto currentSession = mainWindow_->getCurrentSession();
        if (currentSession) {
            connection = QObject::connect(currentSession, &QTermWidget::onNewLine, mainWindow_, match);
        }
    }, Qt::BlockingQueuedConnection);
    const auto cleanup = qScopeGuard([connection]() { QObject::disconnect(connection); });

    auto endTime = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
    int pollCounter = 0;
    constexpr int pollInterval = 4;  // 每4次循环检查一次屏幕内容（约200ms）
    while (std::chrono::steady_clock::now() < endTime) {
        if (shouldStop_()) {
            throw std::runtime_error("interrupted during waitForStrings");
        }

        timerModule_.processTimers();
        if (found->load()) {
            return true;
        }

        if (++pollCounter >= pollInterval) {
            pollCounter = 0;
            QString lastLine;
            QMetaObject::invokeMethod(mainWindow_, "getLastLine",
                                      Qt::BlockingQueuedConnection,
                                      Q_RETURN_ARG(QString, lastLine));
            match(lastLine);
            if (found->load()) {
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}
