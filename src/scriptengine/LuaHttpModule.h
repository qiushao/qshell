#pragma once

#include <functional>
#include <sol/sol.hpp>

class LuaTimerModule;

class LuaHttpModule {
public:
    explicit LuaHttpModule(LuaTimerModule &timerModule);

    void registerAPIs(sol::state &lua, sol::table &qshell, const std::function<bool()> &shouldStop);

private:
    sol::table performHttpRequest(const std::string &method,
                                   const std::string &url,
                                   const std::string &body,
                                   sol::optional<sol::table> options);

    LuaTimerModule &timerModule_;
    std::function<bool()> shouldStop_;
    sol::state *lua_ = nullptr;
};
