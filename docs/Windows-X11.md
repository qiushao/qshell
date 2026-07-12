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

## Known Windows limitation

The upstream VcXsrv TCP transport binds its display port on all local interfaces. A clean Windows profile can therefore show the standard Windows Defender Firewall consent dialog on the first VcXsrv launch. This one-time system prompt is accepted product behavior. Xauthority authentication prevents clients without the generated cookie from opening the display.

## Third-party notices

VcXsrv is distributed under GPLv3; see the upstream project at <https://github.com/marchaesen/vcxsrv>. The portable archive is produced from the repository's VcXsrv 21.1.10.0 installer.

`7zr.exe` is the public-domain reduced console executable from the LZMA SDK / 7-Zip project: <https://www.7-zip.org/>.
