-- 在连接到测试设备或虚拟串口的会话中执行：
-- qshell --script scripts/lua/send_binary_test.lua -- <sessionName>
local ok, err = pcall(function()
    assert(arg[1], "usage: <sessionName>")
    if qshell.session.tabName() == "" then
        assert(not qshell.screen.sendBinary(string.char(0x41)))
    end
    assert(qshell.session.open(arg[1]), "cannot open test session")
    assert(qshell.screen.sendBinary(""))
    assert(qshell.screen.sendBinary(string.char(0x41, 0x00, 0xFF)))
    assert(qshell.screen.sendBinary("\x00\x80\xFF\r\n"))
    assert(qshell.screen.sendBinary("41 00 FF"))
    assert(qshell.screen.sendBinary("\\r\\n"))
    assert(qshell.screen.sendBinary(string.pack("<I2", 0x1234)))
    local bytes = {}
    for value = 0, 255 do
        bytes[#bytes + 1] = string.char(value)
    end
    assert(qshell.screen.sendBinary(table.concat(bytes)))
    qshell.sleep(0.2)
    qshell.session.disconnect()
    assert(not qshell.screen.sendBinary(string.char(0x41)))
end)
if not ok then
    qshell.log(tostring(err))
    qshell.exit(1)
else
    qshell.log("send_binary_test.lua passed")
    qshell.exit(0)
end
