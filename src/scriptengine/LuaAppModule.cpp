#include "LuaAppModule.h"

#include "LuaTimerModule.h"
#include "ui/MainWindow.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>

LuaAppModule::LuaAppModule(MainWindow *mainWindow, LuaTimerModule &timerModule)
    : mainWindow_(mainWindow), timerModule_(timerModule) {}

void LuaAppModule::closeLog() {
    logFile_.reset();
}

// ========== qshell 模块 ==========
void LuaAppModule::registerAPIs(sol::table &qshell, const std::function<void()> &requestStop) {
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

    qshell.set_function("setLogFile", [this](const std::string& path, sol::optional<bool> append) {
        if (path.empty()) {
            logFile_.reset();
            return;
        }
        auto file = std::make_unique<QFile>(QString::fromStdString(path));
        if (!file->open(QIODevice::WriteOnly | (append.value_or(true) ? QIODevice::Append : QIODevice::Truncate))) {
            throw std::runtime_error("setLogFile: " + path + ": " + file->errorString().toStdString());
        }
        logFile_ = std::move(file);
    });

    qshell.set_function("log", [this](const std::string& msg) {
        qDebug() << QString::fromStdString(msg);
        if (logFile_) {
            const QByteArray line = QDateTime::currentDateTime().toString("[yyyy-MM-dd hh:mm:ss:zzz] ").toUtf8()
                                    + QByteArray::fromStdString(msg) + '\n';
            if (logFile_->write(line) != line.size() || !logFile_->flush()) {
                throw std::runtime_error("log: " + logFile_->fileName().toStdString()
                                         + ": " + logFile_->errorString().toStdString());
            }
        }
    });

    // sleep 使用可中断版本并处理定时器
    qshell.set_function("sleep", [this](double seconds) {
        timerModule_.interruptibleSleep(static_cast<int>(seconds * 1000));
    });

    qshell.set_function("getVersionStr", []() {
        return QCoreApplication::applicationVersion().toStdString();
    });

    qshell.set_function("exit", [this, requestStop](sol::optional<int> code) {
        const int exitCode = code.value_or(0);
        requestStop();
        QMetaObject::invokeMethod(mainWindow_, [exitCode]() {
            QCoreApplication::exit(exitCode);
        }, Qt::QueuedConnection);
    });
}
