"""Exercise Lua file logging and cleanup through the actual application."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def run_case(executable, directory, fail):
    first = directory / "日志.log"
    second = directory / "second.log"
    first.write_bytes(b"existing\n")
    script = directory / "log.lua"
    script.write_text('''
qshell.log("CONSOLE_BEFORE")
qshell.setLogFile(arg[1])
qshell.log("FILE_AND_CONSOLE")
qshell.log("中文日志")
local function read(path)
    local file = assert(io.open(path, "rb"))
    local text = file:read("*a")
    file:close()
    return text
end
assert(read(arg[1]) == "existing\\nFILE_AND_CONSOLE\\n中文日志\\n")
local ok, err = pcall(qshell.setLogFile, "missing-parent/log.txt")
assert(not ok and err:find("setLogFile:"))
qshell.log("AFTER_OPEN_FAILURE")
qshell.setLogFile(arg[2])
qshell.log("SECOND_FILE")
qshell.setLogFile("")
qshell.setLogFile("")
qshell.log("CONSOLE_AFTER")
assert(read(arg[2]) == "SECOND_FILE\\n")
qshell.setLogFile(arg[2], true)
qshell.log("APPENDED")
assert(read(arg[2]) == "SECOND_FILE\\nAPPENDED\\n")
qshell.setLogFile(arg[2], false)
assert(read(arg[2]) == "")
qshell.log("OVERWRITTEN")
qshell.log("NEXT_LINE")
assert(read(arg[2]) == "OVERWRITTEN\\nNEXT_LINE\\n")
qshell.setLogFile("/dev/full")
ok, err = pcall(qshell.log, "WRITE_FAILURE")
assert(not ok and err:find("log:"))
qshell.setLogFile(arg[1])
qshell.timer.setTimeout(function() qshell.log("TIMER_LOG") end, 0)
qshell.sleep(0.05)
assert(read(arg[1]) == "existing\\nFILE_AND_CONSOLE\\n中文日志\\nAFTER_OPEN_FAILURE\\nTIMER_LOG\\n")
print("LOG_INTEGRATION_OK")
''' + ('error("intentional cleanup test")\n' if fail else ''), encoding="utf-8")
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen",
               XDG_CONFIG_HOME=str(directory / "config"),
               XDG_DATA_HOME=str(directory / "data"))
    output_path = directory / "application.log"
    with output_path.open("w") as output:
        process = subprocess.Popen(
            [executable, "--script", str(script), "--", str(first), str(second)],
            cwd=directory, stdout=output, stderr=output, env=env)
        try:
            completion = "Running script error" if fail else "Running script finished"
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                text = output_path.read_text(encoding="utf-8")
                if completion in text:
                    assert "LOG_INTEGRATION_OK" in text, text
                    for marker in ("CONSOLE_BEFORE", "FILE_AND_CONSOLE", "CONSOLE_AFTER"):
                        assert marker in text, text
                    targets = []
                    for descriptor in Path(f"/proc/{process.pid}/fd").iterdir():
                        try:
                            targets.append(os.readlink(descriptor))
                        except FileNotFoundError:
                            pass
                    assert str(first) not in targets and str(second) not in targets, targets
                    return
                assert process.poll() is None, text
                time.sleep(0.02)
            raise AssertionError(output_path.read_text(encoding="utf-8"))
        finally:
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=5)


with tempfile.TemporaryDirectory(prefix="qshell-log-") as temp:
    for fail in (False, True):
        directory = Path(temp) / str(fail)
        directory.mkdir()
        run_case(str(Path(sys.argv[1]).resolve()), directory, fail)
print("Lua file logging, append/overwrite, UTF-8, switching, failures, timers and cleanup passed")
