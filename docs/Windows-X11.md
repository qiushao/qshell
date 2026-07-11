# Windows X11 Forwarding

QShell requests SSH X11 forwarding automatically and starts a local Windows X11 server only when the remote side opens the first X11 connection. Users do not need to configure sessions, install VcXsrv manually, or start a separate X server.

Packaging can include either a ready-to-run VcXsrv directory or a VcXsrv installer. A ready-to-run directory is used directly. An installer is copied into the app package and is silently installed on first X11 use into the user's local app data directory.

To package a ready-to-run VcXsrv directory that contains `vcxsrv.exe` and its runtime files:

- Put the files under `resources/x11/`.
- Put the files under `third_party/vcxsrv/`.
- Pass `-DQSHELL_BUNDLED_X11_DIR=C:/path/to/VcXsrv` to CMake.
- Use `.\scripts\windows-build.ps1 -BundledX11Dir C:\path\to\VcXsrv`.

To package an installer:

- Put `vcxsrv*.installer.exe` under `resources/x11-installer/`.
- Put `vcxsrv*.installer.exe` under `third_party/vcxsrv-installer/`.
- Pass `-DQSHELL_BUNDLED_X11_INSTALLER=C:/path/to/vcxsrv.installer.exe` to CMake.
- Use `.\scripts\windows-build.ps1 -BundledX11Installer C:\path\to\vcxsrv.installer.exe`.

At runtime QShell looks for a bundled or previously installed server first. If none exists but a bundled installer is present, QShell runs it with silent installer options into `QStandardPaths::AppLocalDataLocation/x11/vcxsrv`, then starts `vcxsrv.exe`. The server starts on the first free display in `:44` through `:99`, and libssh2 X11 channels are bridged to that local display. If no bundled server or installer is present, QShell keeps SSH login working and only falls back to an already-running local X server on `127.0.0.1:6000`.
