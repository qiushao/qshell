
ret = qshell.session.open("ir")
if (ret == false) then
    qshell.showMessage("open session ir failed")
    return
end

for i = 1, 10 do
    qshell.screen.sendBinary(0xC0)
    qshell.sleep(10)
end
