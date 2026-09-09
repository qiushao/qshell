qshell.setLogFile("/home/qiushao/qshell.log", false)
qshell.log("start ir test")
local ir = qshell.serial.open("/dev/ttyUSB0", {baudRate = 9600})

for i = 1, 10 do
    qshell.log("ir test " .. i)
    ir:writeBinary(0xC0) -- 发送 power 红外码
    qshell.sleep(40)
end

ir:close()
qshell.log("finish ir test")
qshell.setLogFile("")