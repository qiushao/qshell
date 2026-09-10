local tv = {}

function tv.isStart()
    qshell.screen.sendText("\r")
    qshell.sleep(0.1)
    qshell.screen.sendText("echo QSHELL_SERIAL_OK\r")
    qshell.sleep(0.3)
    local str = qshell.screen.getScreenText()
    return str:find("QSHELL_SERIAL_OK", 1, true) ~= nil
end

function tv.closeKernelLog()
    qshell.screen.sendText("\r")
    qshell.screen.sendText("su\r")
    qshell.sleep(0.5)
    qshell.screen.sendText("echo 0 > /proc/sys/kernel/printk\r")
    qshell.sleep(1)
end

function tv.waitStart(timeout)
    qshell.screen.sendText("\r\r")
    return qshell.screen.waitForStrings({"console:/", "shell@"}, timeout)
end

return tv
