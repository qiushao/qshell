# Windows X11 Forwarding

QShell requests SSH X11 forwarding automatically. On Windows it also prepares and starts a private local VcXsrv instance, so users do not need to install VcXsrv, start XLaunch, or configure `DISPLAY`.

## Runtime flow

1. The Windows package contains `x11-runtime/7zr.exe` and a versioned `vcxsrv*.7z` portable archive.
2. On first SSH use, QShell extracts that archive without elevation to `QStandardPaths::AppLocalDataLocation/x11/vcxsrv`.
3. QShell starts VcXsrv on the first free display from `:44` through `:99`.
4. QShell creates a per-process Xauthority file and a random `MIT-MAGIC-COOKIE-1`. The same cookie is sent in the SSH `x11-req`, so VcXsrv does not need the insecure `-ac` option.
5. Incoming libssh2 X11 channels are bridged to the selected loopback TCP port.

The extraction marker includes the archive name and size. Replacing the packaged archive with a new version causes QShell to refresh the per-user runtime automatically.

## Packaging

The default repository runtime is `third_party/vcxsrv-runtime/`. A custom runtime directory can be selected with either:

- CMake: `-DQSHELL_BUNDLED_X11_RUNTIME_DIR=C:/path/to/vcxsrv-runtime`
- PowerShell: `.\scripts\windows-build.ps1 -BundledX11RuntimeDir C:\path\to\vcxsrv-runtime`

The directory must contain:

- `7zr.exe`
- One `vcxsrv*.7z` or `x11-runtime*.7z` archive whose root contains `vcxsrv.exe` and all of its runtime files.

A ready-to-run directory containing `vcxsrv.exe` remains supported through `QSHELL_BUNDLED_X11_DIR`, `resources/x11/`, or `third_party/vcxsrv/`.

If no packaged runtime is present, QShell can fall back to an already-running X server on `127.0.0.1:6000`; automatic startup is unavailable in that fallback configuration.

## Forwarding performance

Linux/macOS sessions run system OpenSSH (`ssh -X`); Windows uses libssh2 and a
loopback TCP connection to VcXsrv. The Windows bridge disables Nagle on both
the SSH connection and the local X11 connection to avoid delaying small X11
requests behind TCP acknowledgements.

The event pump retries all channels on SSH read/write readiness, using
`libssh2_session_block_directions()` to distinguish TCP send backpressure from
waiting for a remote channel-window adjustment. In-flight write buffers and PTY
resize arguments remain stable across `EAGAIN`. Reads and writes can buffer
packets for other channels inside libssh2; those packets trigger a queued pump
without waiting for another OS socket-read event. Per-channel batches are limited
to 256 KiB, and local socket queues apply backpressure, so drawing bursts yield
to the Qt event loop. Final remote drawing bytes drain before an X11 EOF closes
the local connection.

### Reproducing a performance check

Use the same server, VcXsrv version, window size and network conditions for both
builds. Close other benchmark/IDE instances first. Run one build at a time:

```powershell
.\qshell.exe --script D:/projects/qt/qshell/scripts/lua/x11_benchmark.lua -- <session-name> D:/temp/x11-result.log
```

The server must have `x11perf` installed. The script runs:

```sh
x11perf -repeat 3 -time 2 -rect500 -putimage500
```

Compare the aggregate `trep` rates, then run `studio.sh` and check editor
scrolling, menu opening, window resizing and terminal input during drawing.
`x11perf` operation rates are not Android Studio frame rates. IDE indexing,
remote CPU load, Java rendering and VcXsrv rendering can still affect responsiveness;
a Windows bridge fix does not make its rendering backend identical to Linux.

The Windows-only `ssh_x11_pump_test` regression test uses deterministic transport
backpressure to exercise writable-only recovery, inbound waits without spinning,
stable write/resize retries, buffered cross-channel reads, bounded drawing batches,
local backpressure and EOF draining:

```powershell
cmake --build build --config Release --target ssh_x11_pump_test
ctest --test-dir build -C Release -R ssh_x11_pump_test --output-on-failure
```

When running from a build tree, Qt's `bin` directory must be on `PATH`, and
`QT_PLUGIN_PATH` must point to its `plugins` directory so the offscreen test
platform is available.

## Known Windows limitation

The upstream VcXsrv TCP transport binds its display port on all local interfaces. A clean Windows profile can therefore show the standard Windows Defender Firewall consent dialog on the first VcXsrv launch. This one-time system prompt is accepted product behavior. Xauthority authentication prevents clients without the generated cookie from opening the display.

## Third-party notices

VcXsrv is distributed under GPLv3; see the upstream project at <https://github.com/marchaesen/vcxsrv>. The portable archive is produced from the repository's VcXsrv 21.1.10.0 installer.

`7zr.exe` is the public-domain reduced console executable from the LZMA SDK / 7-Zip project: <https://www.7-zip.org/>.
