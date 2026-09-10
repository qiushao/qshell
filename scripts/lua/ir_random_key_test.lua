local ir = require("common/ir")

qshell.setLogFile("/home/qiushao/qshell.log", false)
local remote = ir.new("/dev/ttyUSB2", {baudRate = 9600})

for i = 1, 10 do
    local key = remote:randomKey()
    qshell.log("random key " .. i .. ", keycode = " .. key)
    remote:click(key)
    qshell.sleep(3)
end

remote:release()
qshell.setLogFile("")
