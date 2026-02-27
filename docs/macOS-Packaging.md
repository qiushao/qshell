# macOS Packaging Guide for qshell

This guide explains how to build and package qshell for macOS distribution.

## Prerequisites

1. **macOS** (10.13 or later recommended)
2. **Xcode Command Line Tools**
   ```bash
   xcode-select --install
   ```
3. **Homebrew** (for package management)
   ```bash
   /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
   ```
4. **Qt5**
   ```bash
   brew install cmake qt@5
   export Qt5_DIR=$(brew --prefix qt@5)
   ```

## Quick Start

### Automated Build and Package

Run the build script:
```bash
./scripts/macos-build.sh
```

This script will:
1. Check dependencies
2. Convert PNG icon to ICNS format (if needed)
3. Build the application
4. Prompt to create a DMG package

### Manual Build Steps

```bash
# Create build directory
cmake -B build -S .

# Build
cmake --build build --config Release

# Install (optional)
sudo cmake --install build
```

### Create DMG Package

```bash
cd build
cpack -G DragNDrop
```

The DMG file will be created in the build directory with the name:
`qshell-{version}-Darwin.dmg`

## Icon Creation

macOS applications require `.icns` format icons. The build process needs:

1. **Source PNG**: `src/resources/images/qshell.png` (128x128 or larger)
2. **ICNS file**: `src/resources/images/qshell.icns`

### Automatic Conversion

On macOS, run:
```bash
./scripts/macos-create-icon.sh
```

This script uses macOS built-in tools (`sips` and `iconutil`) to:
- Convert the PNG to multiple sizes (16x16 to 1024x1024)
- Package them into a `.icns` file

### Manual Conversion (Alternative)

If you don't have access to macOS, you can use online converters:
- https://iconverticons.com/online/
- https://www.img2icnsapp.com/

Required icon sizes:
- 16x16
- 32x32
- 64x64
- 128x128
- 256x256
- 512x512
- 1024x1024

## Bundle Structure

When packaged, qshell will be structured as a macOS application bundle:

```
qshell.app/
├── Contents/
│   ├── Info.plist          # Application metadata
│   ├── MacOS/
│   │   └── qshell          # Executable binary
│   ├── Resources/
│   │   └── qshell.icns     # Application icon
│   └── Frameworks/         # Qt frameworks (if bundled)
```

## Code Signing (Optional)

For distribution outside the Mac App Store, you may want to sign your application:

```bash
# Sign the application
codesign --force --deep --sign "Developer ID Application: YOUR_NAME" \
    build/qshell.app

# Verify signature
codesign --verify --verbose build/qshell.app

# Create a signed DMG
codesign --force --deep --sign "Developer ID Application: YOUR_NAME" \
    build/qshell-*.dmg
```

## Notarization (Optional)

For macOS Catalina (10.15) and later, notarization is recommended:

```bash
# Upload for notarization
xcrun notarytool submit build/qshell-*.dmg \
    --keychain-profile "AC_PASSWORD" \
    --wait

# Staple the notarization ticket
xcrun stapler staple build/qshell-*.dmg
```

## Troubleshooting

### Qt5 not found
```bash
# Set Qt5_DIR explicitly
export Qt5_DIR=/path/to/qt5
# Common locations:
# - /usr/local/opt/qt@5 (Intel Mac, Homebrew)
# - /opt/homebrew/opt/qt@5 (Apple Silicon, Homebrew)
```

### Icon conversion fails
- Ensure you're running the script on macOS
- Check that the source PNG exists at `src/resources/images/qshell.png`
- Verify the PNG is a square image (equal width and height)

### CPack fails
- Make sure the ICNS file exists before running cpack
- Check that `Info.plist` is properly configured
- Run `cmake --build build` successfully before packaging

## Distribution

The generated DMG file can be:
1. Uploaded to GitHub Releases
2. Distributed via your website
3. Shared directly with users

Users can:
1. Download the DMG
2. Open it and drag qshell.app to Applications
3. Run from Applications folder

## Notes

- The DMG is compressed using UDZO format for smaller file size
- The application bundle includes all necessary resources
- Qt frameworks can be bundled using `macdeployqt` if needed (not currently configured)
