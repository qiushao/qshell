-- 独立打开两个串口，无需创建或切换终端会话。
-- 参数：开发板端口、波特率、红外端口、波特率。
-- 请根据设备实际设置修改波特率；默认均为 115200，8N1，无流控。
local board = qshell.serial.open("/dev/ttyUSB2", {
    baudRate = 115200
})
local ir = qshell.serial.open("/dev/ttyUSB0", {
    baudRate = 9600
})

-- Android shell 文本收发；换行需显式指定。
board:writeText("echo QSHELL_SERIAL_OK\r")
local output = ""
for i = 1, 10 do
    output = output .. board:readText(4096, 200)
end
qshell.log(output)

-- 发送一个原始 C0 字节，触发红外控制器的 power 红外码。
ir:writeBinary(0xC0)

-- 如控制器有应答，按字节读取并以十六进制记录；无应答时返回空数组。
local reply = ir:readBinary(256, 200)
local hex = {}
for i, byte in ipairs(reply) do
    hex[i] = string.format("%02X", byte)
end
qshell.log("IR RX: " .. table.concat(hex, " "))

ir:close()
board:close()
