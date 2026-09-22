#pragma once

#include <sol/sol.hpp>

class MainWindow;

class LuaSessionModule {
public:
    explicit LuaSessionModule(MainWindow *mainWindow);

    void registerAPIs(sol::table &qshell);

private:
    MainWindow *mainWindow_;
};
