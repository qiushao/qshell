"""Exercise the actual application, script thread, and serial cleanup on Linux."""
import os
from pathlib import Path
import pty
import select
import subprocess
import sys
import tempfile
import time


def run_case(executable, directory, fail):
    masters, slaves = zip(pty.openpty(), pty.openpty())
    paths = [os.ttyname(fd) for fd in slaves]
    script = directory / "serial.lua"
    script.write_text('''
board = qshell.serial.open(arg[1])
ir = qshell.serial.open(arg[2], {baudRate = 9600})
assert(board:writeText("echo hello\\r") == 11)
local text = ""
while #text < 7 do
    local chunk = board:readText(7 - #text, 1000)
    assert(#chunk > 0, "missing board reply")
    text = text .. chunk
end
assert(text == "hello\\r\\n")
assert(ir:writeBinary(0xC0) == 1)
local bytes = {}
while #bytes < 256 do
    local chunk = ir:readBinary(256 - #bytes, 1000)
    assert(#chunk > 0, "missing binary reply")
    for _, byte in ipairs(chunk) do bytes[#bytes + 1] = byte end
end
for i = 1, 256 do assert(bytes[i] == i - 1) end
print("SERIAL_INTEGRATION_OK")
''' + ('error("intentional cleanup test")\n' if fail else ''), encoding="utf-8")
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen",
               XDG_CONFIG_HOME=str(directory / "config"),
               XDG_DATA_HOME=str(directory / "data"))
    log_path = directory / "application.log"
    process = None
    try:
        with log_path.open("w") as log:
            process = subprocess.Popen([executable, "--script", str(script), "--", *paths],
                                       stdout=log, stderr=log, env=env)
            received = [b"", b""]
            expected = [b"echo hello\r", b"\xc0"]
            replies = [b"hello\r\n", bytes(range(256))]
            completion = "Running script error" if fail else "Running script finished"
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                ready, _, _ = select.select(masters, [], [], 0.02)
                for fd in ready:
                    index = masters.index(fd)
                    received[index] += os.read(fd, 4096)
                    assert expected[index].startswith(received[index]), received
                    if received[index] == expected[index]:
                        os.write(fd, replies[index])
                output = log_path.read_text()
                if completion in output:
                    assert "SERIAL_INTEGRATION_OK" in output, output
                    assert received == expected, received
                    # Lua globals retain both handles; the running app must have
                    # released the underlying ports before signaling completion.
                    descriptors = Path(f"/proc/{process.pid}/fd")
                    targets = []
                    for descriptor in descriptors.iterdir():
                        try:
                            targets.append(os.readlink(descriptor))
                        except FileNotFoundError:
                            pass
                    assert not set(paths).intersection(targets), targets
                    assert process.poll() is None, "application unexpectedly exited"
                    return
                assert process.poll() is None, output
            raise AssertionError(log_path.read_text())
    finally:
        if process is not None and process.poll() is None:
            process.terminate()
            process.wait(timeout=5)
        for fd in masters + slaves:
            os.close(fd)


with tempfile.TemporaryDirectory(prefix="qshell-serial-") as temp:
    for fail in (False, True):
        run_case(sys.argv[1], Path(temp), fail)
print("Application Lua serial I/O and normal/error cleanup passed")
