# Portable VcXsrv runtime

This directory is copied into Windows packages as `x11-runtime/`.

- `vcxsrv-21.1.10.0.7z` contains the portable VcXsrv files extracted from the upstream NSIS installer.
- `7zr.exe` is the public-domain reduced extractor from the 7-Zip LZMA SDK.

QShell extracts the archive into the current user's local application-data directory on first X11 use. No installer or administrator elevation is required.
