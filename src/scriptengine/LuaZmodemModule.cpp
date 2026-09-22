#include "LuaZmodemModule.h"

#include "ui/MainWindow.h"

LuaZmodemModule::LuaZmodemModule(MainWindow *mainWindow)
    : mainWindow_(mainWindow) {}

void LuaZmodemModule::registerAPIs(sol::table &qshell) {
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
