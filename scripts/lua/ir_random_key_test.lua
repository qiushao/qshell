local ir = require("common/ir")

qshell.setLogFile("qshell.log", false)
local remote = ir.new("/dev/ttyUSB2", {baudRate = 9600})

for i = 1, 100 do
    local key = remote:randomKey()
    qshell.log("random key " .. i .. ", keycode = " .. key)
    remote:click(key)
    qshell.sleep(1)
end

remote:release()
qshell.setLogFile("")
