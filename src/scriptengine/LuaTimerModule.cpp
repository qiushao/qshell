#include "LuaTimerModule.h"

#include <QDebug>
#include <algorithm>
#include <thread>
#include <utility>

// 可中断的 sleep，同时处理定时器
void LuaTimerModule::interruptibleSleep(int milliseconds)
{
    auto endTime = std::chrono::steady_clock::now()
                   + std::chrono::milliseconds(milliseconds);

    while (std::chrono::steady_clock::now() < endTime) {
        if (shouldStop_ && shouldStop_()) {
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
void LuaTimerModule::processTimers()
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

void LuaTimerModule::reset() {
    std::lock_guard<std::mutex> lock(timersMutex_);
    timers_.clear();
    nextTimerId_ = 1;
}

// ========== qshell.timer 模块 ==========
void LuaTimerModule::registerAPIs(sol::table &qshell, const std::function<bool()> &shouldStop)
{
    shouldStop_ = shouldStop;

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
