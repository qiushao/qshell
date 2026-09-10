package.loaded["common/ir"] = nil
package.loaded["common/tv"] = nil
local ir = require("common/ir")
local tv = require("common/tv")

qshell.setLogFile("qshell.log", false)
local remote = ir.new("/dev/ttyUSB2", {baudRate = 9600})

-- 创建日志保存目录
qshell.screen.sendText("su\r")
qshell.screen.sendText("rm -rf /storage/sda1/logs/\r")
qshell.sleep(1)
qshell.screen.sendText("mkdir -p /storage/sda1/logs/\r")
qshell.sleep(1)

for i = 1, 10 do
    local isStart = tv.isStart()
    remote:click(ir.keycode.power)
    if (isStart) then
         -- 待机
        qshell.log("reboot test " .. i .. ", 待机")
        qshell.sleep(30)
        goto continue -- 跳转到标签处，相当于 continue， lua 没有 continue 语法
    end

    -- 开机
    qshell.log("reboot test " .. i .. ", 开机")
    qshell.sleep(8)
    ret = tv.waitStart(20)
    if (ret == false) then
        qshell.log("wait tv start failed")
        goto continue
    end

    tv.closeKernelLog()

    -- 抓日志
    qshell.screen.sendText("logcat > /data/reboot" .. i .. ".log&\r")
    qshell.sleep(1)
    qshell.screen.sendText("dmesg -w > /data/dmesg" .. i .. ".log&\r")
    qshell.sleep(40)

    -- 这里可以添加其他测试项目

    -- 停止抓日志
    qshell.screen.sendText("\r")
    qshell.screen.sendText("pkill logcat\r")
    qshell.sleep(1)
    qshell.screen.sendText("pkill dmesg\r")
    qshell.sleep(1)

    -- 靠日志到 u 盘
    qshell.screen.sendText("mv /data/reboot" .. i .. ".log " .. "/storage/sda1/logs/\r")
    qshell.sleep(2)
    qshell.screen.sendText("mv /data/dmesg" .. i .. ".log " .. "/storage/sda1/logs/\r")
    qshell.sleep(3)

    ::continue:: -- 定义一个continue标签，以便模拟 continue 语法,  lua 没有 continue 语法
end

remote:release()
qshell.setLogFile("")
