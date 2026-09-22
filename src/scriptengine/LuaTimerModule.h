#pragma once

#include <chrono>
#include <functional>
#include <mutex>
#include <sol/sol.hpp>
#include <vector>

class LuaTimerModule {
public:
    void registerAPIs(sol::table &qshell, const std::function<bool()> &shouldStop);

    // 处理所有到期的定时器
    void processTimers();
    // 可中断的 sleep，同时处理定时器回调
    void interruptibleSleep(int milliseconds);
    // 清理所有定时器并重置 id 计数（脚本启动前调用）
    void reset();

private:
    struct TimerInfo {
        int id;
        std::chrono::steady_clock::time_point nextTrigger;
        int intervalMs;  // 0 = 单次定时器, >0 = 重复定时器
        sol::function callback;
        bool active;
    };

    std::function<bool()> shouldStop_;
    std::vector<TimerInfo> timers_;
    std::mutex timersMutex_;
    int nextTimerId_ = 1;
};
