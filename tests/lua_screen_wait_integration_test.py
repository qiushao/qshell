"""Exercise screen string waits through the application and a virtual serial port."""
import json
import os
from pathlib import Path
import pty
import select
import subprocess
import sys
import tempfile
import time


with tempfile.TemporaryDirectory(prefix="qshell-screen-wait-") as temp:
    directory = Path(temp)
    master, slave = pty.openpty()
    config_dir = directory / "config" / "qiushao" / "qshell"
    config_dir.mkdir(parents=True)
    (config_dir / "config.json").write_text(json.dumps({
        "sessions": [{
            "id": "wait-test", "name": "wait-test", "protocolType": 2,
            "serialConfig": {
                "portName": os.ttyname(slave), "baudRate": 115200,
                "dataBits": 8, "parity": 0, "stopBits": 1,
                "flowControl": 0, "dataMode": "text",
            },
        }],
    }))
    script = directory / "wait.lua"
    script.write_text('''
assert(qshell.session.open("wait-test"))
local wait = qshell.screen.waitForStrings
assert(type(wait) == "function", "waitForStrings is missing")
assert(wait({}, 10) == false)
assert(wait({"missing"}, 0) == false)
assert(wait({"missing"}, -1) == false)
for _, bad in ipairs({{"ok", 1}, {true}, {[1] = "a", [3] = "b"}, {key = "a"}}) do
    assert(not pcall(wait, bad, 1), "invalid array accepted")
end
local fired = false
qshell.timer.setTimeout(function() fired = true end, 50)
qshell.screen.sendText("first\\n")
assert(wait({"FIRST", "absent"}, 2) == true)
assert(fired, "timer did not run while waiting")
qshell.screen.sendText("second\\n")
assert(wait({"absent", "SECOND"}, 2) == true)
qshell.screen.sendText("prompt\\n")
assert(wait({"absent", "提示符: $"}, 2) == true)
qshell.screen.sendText("existing\\n")
qshell.sleep(0.4)
assert(wait({"absent", "EXISTING"}, 2) == true)
qshell.screen.sendText("legacy\\n")
assert(qshell.screen.waitForString("LEGACY", 2) == true)
assert(qshell.screen.waitForString("absent", 1) == false)
assert(wait({"legacy", "L.GACY"}, 1) == false)
qshell.screen.sendText("again\\n")
assert(wait({"absent", "AGAIN"}, 2) == true)
print("SCREEN_WAIT_INTEGRATION_OK")
''', encoding="utf-8")
    replies = {
        # Fill the screen so unterminated prompts occupy its last row.
        b"first": [b"\r\n" * 100 + b"FIRST\r\ntrailing\r\n"],
        b"second": [b"SECOND\r\ntrailing\r\n"],
        b"prompt": ["提示".encode(), "符: $".encode()],
        b"existing": [b"\r\nEXISTING"],
        b"legacy": [b"\r\nLEGACY"],
        b"again": [b"\r\nAGAIN\r\ntrailing\r\n"],
    }
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen",
               XDG_CONFIG_HOME=str(directory / "config"),
               XDG_DATA_HOME=str(directory / "data"))
    output_path = directory / "application.log"
    process = None
    try:
        with output_path.open("w") as output:
            process = subprocess.Popen(
                [str(Path(sys.argv[1]).resolve()), "--script", str(script)],
                cwd=directory, stdout=output, stderr=output, env=env)
            pending = b""
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                ready, _, _ = select.select([master], [], [], 0.02)
                if ready:
                    pending += os.read(master, 4096)
                    while b"\n" in pending:
                        command, pending = pending.split(b"\n", 1)
                        for reply in replies.pop(command):
                            time.sleep(0.1)
                            os.write(master, reply)
                text = output_path.read_text(encoding="utf-8")
                assert "Running script error" not in text, text
                if "Running script finished" in text:
                    assert "SCREEN_WAIT_INTEGRATION_OK" in text, text
                    assert not replies, replies
                    break
                assert process.poll() is None, text
            else:
                raise AssertionError(output_path.read_text(encoding="utf-8"))
    finally:
        if process is not None:
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=5)
        os.close(master)
        os.close(slave)
print("Screen waits: candidates, new lines, partial prompts, timeout, timers and legacy API passed")
