#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QStyleFactory>
#include <QTimer>
#include "ui/MainWindow.h"
#include "core/ConfigManager.h"
#include "core/LanguageManager.h"

#ifdef _WIN32
#include <windows.h>
#include <cstdio>
#endif

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}
#include <sol/sol.hpp>

static void showConsole(bool show)
{
#ifdef _WIN32
    if (show)
    {
        AllocConsole();

        // 把 stdout / stderr 重定向到控制台（这样 printf / std::cout / qDebug 都能看到）
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        SetConsoleTitleW(L"Debug Console");
    }
    else
    {
        FreeConsole();
    }
#endif
}

int main(int argc, char *argv[])
{
    Q_INIT_RESOURCE(qtermwidget);
    QApplication a(argc, argv);

    // 设置应用信息
    QCoreApplication::setApplicationName("qshell");
    QCoreApplication::setApplicationVersion(APP_VERSION);
    QCoreApplication::setOrganizationName("qiushao");
    QCoreApplication::setOrganizationDomain("https://github.com/qiushao/qshell");
    LanguageManager::instance();
    showConsole(ConfigManager::instance()->globalSettings().debug);

    // 读取版本信息
    QString version = QCoreApplication::applicationVersion();
    qDebug() << "Version:" << version;

    QCommandLineParser parser;
    parser.setApplicationDescription("QShell Terminal Emulator");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption scriptOption(
        QStringList() << "script",
        "Execute Lua script on startup.",
        "path"
    );
    parser.addOption(scriptOption);
    parser.addPositionalArgument("script-args",
                                 "Arguments passed to Lua script (use `--` before args).");
    parser.process(a);

    const QString startupScriptPath = parser.value(scriptOption).trimmed();
    const QStringList startupScriptArgs = parser.positionalArguments();

    MainWindow w;
    w.show();

    if (!startupScriptPath.isEmpty()) {
        QTimer::singleShot(0, &w, [startupScriptPath, startupScriptArgs, &w]() {
            w.runScriptAtStartup(startupScriptPath, startupScriptArgs);
        });
    }

    return QApplication::exec();
}
