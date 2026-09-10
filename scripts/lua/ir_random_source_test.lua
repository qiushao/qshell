local ir = require("common/ir")

qshell.setLogFile("qshell.log", false)
local remote = ir.new("/dev/ttyUSB2", {baudRate = 9600})
local sourceCount = 6

for i = 1, 100 do
    remote:click(ir.keycode.input)
    qshell.sleep(1)

    local direction = math.random(2) == 1 and "up" or "down"
    local steps = math.random(1, sourceCount - 1)
    qshell.log("random source " .. i .. ", direction = " .. direction .. ", steps = " .. steps)
    for j = 1, steps do
        remote:click(ir.keycode[direction])
        qshell.sleep(0.3)
    end

    remote:click(ir.keycode.ok)
    qshell.sleep(5)
end

remote:release()
qshell.setLogFile("")
