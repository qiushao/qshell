#pragma once

#include <sol/sol.hpp>

class MainWindow;

class LuaZmodemModule {
public:
    explicit LuaZmodemModule(MainWindow *mainWindow);

    void registerAPIs(sol::table &qshell);

private:
    MainWindow *mainWindow_;
};
