-- qshell --script scripts/lua/x11_benchmark.lua -- <session-name> [result-log]
-- Requires x11perf on the SSH server. Keep other drawing workloads idle.
assert(arg[1], "usage: <session-name> [result-log]")
if arg[2] then qshell.setLogFile(arg[2], false) end
assert(qshell.session.open(arg[1]), "cannot open SSH session")
qshell.sleep(2)
local marker = "QSHELL_X11_BENCH_DONE"
-- Splitting the marker prevents matching the terminal's command echo.
qshell.screen.sendText(
    "if command -v x11perf >/dev/null; then " ..
    "x11perf -repeat 3 -time 2 -rect500 -putimage500 2>&1 | " ..
    "awk '/trep|unable|error|Error/ {print}'; " ..
    "else echo 'x11perf is not installed'; fi; " ..
    "echo; echo QSHELL_X11_BENCH_\"DONE\"\r")
assert(qshell.screen.waitForString(marker, 120), "X11 benchmark timed out; inspect the terminal")
qshell.log(qshell.screen.getScreenText())
-- Leave the session available for studio.sh and manual interaction checks.
