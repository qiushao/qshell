<#
.SYNOPSIS
    QShell build and package script for Windows.

.PARAMETER BuildOnly
    Build only, skip packaging.

.PARAMETER Version
    Version string for the ZIP filename. If omitted, the script tries the latest git tag and then falls back to 1.0.0.

.PARAMETER BuildType
    Build type: Release or Debug.

.PARAMETER OutputDir
    Output directory.

.PARAMETER BundledX11Dir
    Optional directory that contains vcxsrv.exe and its runtime files.

.PARAMETER BundledX11RuntimeDir
    Optional directory containing a portable VcXsrv .7z archive and 7zr.exe.

.EXAMPLE
    .\windows-build.ps1 -BuildOnly
    .\windows-build.ps1 -Version "1.0.0"
    .\windows-build.ps1 -BundledX11Dir "C:\tools\VcXsrv"
    .\windows-build.ps1 -BundledX11RuntimeDir "C:\tools\vcxsrv-runtime"
#>

param(
    [switch]$BuildOnly,
    [string]$Version,
    [ValidateSet("Release", "Debug")]
    [string]$BuildType = "Release",
    [string]$OutputDir = "deploy",
    [string]$BundledX11Dir = $env:QSHELL_BUNDLED_X11_DIR,
    [string]$BundledX11RuntimeDir = $env:QSHELL_BUNDLED_X11_RUNTIME_DIR
)

$ErrorActionPreference = "Stop"

function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host " $Message" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
}

function Write-Success {
    param([string]$Message)
    Write-Host "[SUCCESS] $Message" -ForegroundColor Green
}

function Write-Info {
    param([string]$Message)
    Write-Host "[INFO] $Message" -ForegroundColor Yellow
}

function Write-Err {
    param([string]$Message)
    Write-Host "[ERROR] $Message" -ForegroundColor Red
}

if (-not $Version) {
    try {
        $GitTag = git describe --tags --abbrev=0 2>$null
        if ($LASTEXITCODE -eq 0 -and $GitTag) {
            $Version = $GitTag -replace '^[vV]', ''
        } else {
            $Version = "1.0.0"
        }
    } catch {
        $Version = "1.0.0"
    }
}

Write-Host ""
Write-Info "Building version: $Version"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptDir

Push-Location $ProjectRoot

try {
    Write-Step "CMake Configure"

    $BuildDir = "build"
    if (-not (Test-Path $BuildDir)) {
        New-Item -ItemType Directory -Path $BuildDir | Out-Null
    }

    $CMakeArgs = @(
        "-B", $BuildDir,
        "-S", ".",
        "-DAPP_VERSION=$Version",
        "-DCMAKE_BUILD_TYPE=$BuildType"
    )
    if ($BundledX11Dir) {
        $CMakeArgs += "-DQSHELL_BUNDLED_X11_DIR=$BundledX11Dir"
        Write-Info "Bundled X11 directory: $BundledX11Dir"
    }
    if ($BundledX11RuntimeDir) {
        $CMakeArgs += "-DQSHELL_BUNDLED_X11_RUNTIME_DIR=$BundledX11RuntimeDir"
        Write-Info "Portable X11 runtime: $BundledX11RuntimeDir"
    }

    cmake @CMakeArgs
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed"
    }
    Write-Success "CMake configure completed"

    Write-Step "Building ($BuildType)"

    cmake --build $BuildDir --config $BuildType --parallel
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed"
    }
    Write-Success "Build completed"

    if ($BuildOnly) {
        Write-Info "Build only mode, skipping package"
        Pop-Location
        exit 0
    }

    Write-Step "Creating deploy directory"

    if (Test-Path $OutputDir) {
        Remove-Item -Path $OutputDir -Recurse -Force
    }
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
    Write-Success "Deploy directory created: $OutputDir"

    Write-Step "Copying executable"

    $PossiblePaths = @(
        "build/src/$BuildType/qshell.exe",
        "build/src/qshell.exe",
        "build/$BuildType/qshell.exe",
        "build/qshell.exe"
    )

    $ExePath = $null
    foreach ($Path in $PossiblePaths) {
        if (Test-Path $Path) {
            $ExePath = $Path
            break
        }
    }

    if ($null -eq $ExePath) {
        throw "Cannot find qshell.exe, please check build output path"
    }

    Write-Info "Found executable: $ExePath"
    Copy-Item -Path $ExePath -Destination "$OutputDir/" -Force
    Write-Success "Copied qshell.exe"

    $ExeDir = Split-Path -Parent $ExePath
    $BuildX11Dir = Join-Path $ExeDir "x11"
    if (Test-Path $BuildX11Dir) {
        Copy-Item -Path $BuildX11Dir -Destination "$OutputDir/x11" -Recurse -Force
        Write-Info "Copied bundled X11 server"
    }

    $BuildX11RuntimeDir = Join-Path $ExeDir "x11-runtime"
    if (Test-Path $BuildX11RuntimeDir) {
        Copy-Item -Path $BuildX11RuntimeDir -Destination "$OutputDir/x11-runtime" -Recurse -Force
        Write-Info "Copied portable X11 runtime"
    }

    Write-Step "Deploying Qt dependencies"

    $CurrentDir = Get-Location
    Set-Location $OutputDir

    if ($BuildType -eq "Debug") {
        windeployqt --debug --no-translations --no-system-d3d-compiler --no-opengl-sw qshell.exe
    } else {
        windeployqt --release --no-translations --no-system-d3d-compiler --no-opengl-sw qshell.exe
    }

    if ($LASTEXITCODE -ne 0) {
        Set-Location $CurrentDir
        throw "windeployqt failed"
    }

    Set-Location $CurrentDir
    Write-Success "Qt dependencies deployed"

    Write-Step "Copying additional files"

    if (Test-Path "config") {
        New-Item -ItemType Directory -Path "$OutputDir/config" -Force | Out-Null
        Copy-Item -Path "config/*" -Destination "$OutputDir/config/" -Recurse -Force
        Write-Info "Copied config directory"
    }

    if (Test-Path "resources") {
        Copy-Item -Path "resources/*" -Destination "$OutputDir/" -Recurse -Force
        Write-Info "Copied resources directory"
    }

    if (Test-Path "README.md") {
        Copy-Item -Path "README.md" -Destination "$OutputDir/" -Force
    }

    if (Test-Path "LICENSE") {
        Copy-Item -Path "LICENSE" -Destination "$OutputDir/" -Force
    }

    Write-Success "Additional files copied"

    Write-Step "Creating ZIP archive"

    $ZipName = "qshell-$Version-win64.zip"
    if (Test-Path $ZipName) {
        Remove-Item -Path $ZipName -Force
    }

    Compress-Archive -Path "$OutputDir/*" -DestinationPath $ZipName -CompressionLevel Optimal
    Write-Success "ZIP archive created: $ZipName"

    $ZipInfo = Get-Item $ZipName
    $SizeMB = [math]::Round($ZipInfo.Length / 1MB, 2)
    Write-Info "File size: $SizeMB MB"

    if ($env:GITHUB_ENV) {
        "ZIP_NAME=$ZipName" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
    }

    Write-Step "Build and package completed!"
    Write-Host ""
    Write-Host "Output files:" -ForegroundColor Green
    Write-Host "  - $OutputDir/ (deploy directory)" -ForegroundColor White
    Write-Host "  - $ZipName (zip archive)" -ForegroundColor White
    Write-Host ""
} catch {
    Write-Err $_.Exception.Message
    Pop-Location
    exit 1
}

Pop-Location
exit 0
