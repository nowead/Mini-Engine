# WebAssembly Build Script for Windows
# Usage:
#   .\scripts\wasm.ps1 setup          # Install Emscripten SDK (one-time)
#   .\scripts\wasm.ps1 build          # Configure + build WASM
#   .\scripts\wasm.ps1 serve          # Build + serve at http://localhost:8000
#   .\scripts\wasm.ps1 serve -Open    # Build + serve + open browser
#   .\scripts\wasm.ps1 clean          # Remove build_wasm/

param(
    [Parameter(Position = 0)]
    [ValidateSet("setup", "build", "serve", "clean")]
    [string]$Command = "serve",

    [switch]$Open,

    [string]$EmsdkDir = "$env:USERPROFILE\emsdk",
    [string]$EmsdkVersion = "4.0.10",
    [string]$BuildDir = "build_wasm",
    [string]$Target = "MiniEngine"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path $MyInvocation.MyCommand.Path
$ProjectDir = Split-Path $ScriptDir
Set-Location $ProjectDir

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

function Write-Step([string]$msg) { Write-Host "`n==> $msg" -ForegroundColor Cyan }
function Write-Ok([string]$msg)   { Write-Host "    $msg" -ForegroundColor Green }
function Write-Warn([string]$msg) { Write-Host "    $msg" -ForegroundColor Yellow }
function Write-Err([string]$msg)  { Write-Host "ERROR: $msg" -ForegroundColor Red; exit 1 }

# Find nmake.exe from Visual Studio 2022.
# Ninja cannot run .bat wrappers (em++.bat) via CreateProcess on Windows.
# NMake executes them through cmd.exe, so it works correctly.
function Find-NMakeDir {
    $pattern = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\*\bin\Hostx64\x64\nmake.exe"
    $hit = Get-Item $pattern -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($hit) { return $hit.DirectoryName }
    return $null
}

function Invoke-Emsdk([string]$CmdArgs) {
    $EmsdkEnv = Join-Path $EmsdkDir "emsdk_env.bat"
    if (-not (Test-Path $EmsdkEnv)) {
        Write-Err "emsdk not found at '$EmsdkDir'. Run: .\scripts\wasm.ps1 setup"
    }

    # Write a temporary batch file — avoids PowerShell→cmd quote escaping issues.
    $tmpBat = [System.IO.Path]::GetTempFileName() -replace '\.tmp$', '.bat'
    try {
        $nmakeDir = Find-NMakeDir
        $lines = [System.Collections.Generic.List[string]]::new()
        $lines.Add("@echo off")
        $lines.Add("call `"$EmsdkEnv`"")
        if ($nmakeDir) {
            $lines.Add("set PATH=$nmakeDir;%PATH%")
        }
        $lines.Add($CmdArgs)

        [System.IO.File]::WriteAllText(
            $tmpBat,
            ($lines -join "`r`n"),
            [System.Text.Encoding]::ASCII
        )
        cmd /c $tmpBat
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    } finally {
        Remove-Item $tmpBat -ErrorAction SilentlyContinue
    }
}

# ---------------------------------------------------------------------------
# setup — install emsdk
# ---------------------------------------------------------------------------

if ($Command -eq "setup") {
    Write-Step "Emscripten SDK Setup (version $EmsdkVersion)"

    if (-not (Test-Path (Join-Path $EmsdkDir "emsdk_env.bat"))) {
        if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
            Write-Err "git is not in PATH. Install Git for Windows first."
        }
        Write-Step "Cloning emsdk..."
        git clone https://github.com/emscripten-core/emsdk.git $EmsdkDir
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    } else {
        Write-Ok "emsdk directory found at $EmsdkDir — installing/activating $EmsdkVersion"
    }

    Write-Step "Installing Emscripten $EmsdkVersion..."
    Push-Location $EmsdkDir
    .\emsdk.bat install $EmsdkVersion
    .\emsdk.bat activate $EmsdkVersion
    Pop-Location

    Write-Step "Verifying..."
    Invoke-Emsdk "emcc --version"

    Write-Ok ""
    Write-Ok "Setup complete! Next steps:"
    Write-Ok "  .\scripts\wasm.ps1 build    # Build WASM"
    Write-Ok "  .\scripts\wasm.ps1 serve    # Build + run in browser"
    exit 0
}

# ---------------------------------------------------------------------------
# clean
# ---------------------------------------------------------------------------

if ($Command -eq "clean") {
    Write-Step "Cleaning $BuildDir..."
    if (Test-Path $BuildDir) {
        Remove-Item -Recurse -Force $BuildDir
        Write-Ok "Removed $BuildDir"
    } else {
        Write-Warn "$BuildDir does not exist, nothing to clean."
    }
    exit 0
}

# ---------------------------------------------------------------------------
# build / serve — configure + compile
# ---------------------------------------------------------------------------

$nmakeDir = Find-NMakeDir
if (-not $nmakeDir) {
    Write-Warn "nmake.exe not found in VS 2022. Build may fail."
    Write-Warn "Expected path: C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\*\bin\Hostx64\x64"
} else {
    Write-Ok "nmake found: $nmakeDir"
}

# Configure (skip if CMakeCache.txt already exists)
if (-not (Test-Path (Join-Path $BuildDir "CMakeCache.txt"))) {
    Write-Step "Configuring CMake for WebAssembly (NMake Makefiles)..."
    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

    # Use NMake Makefiles — Ninja cannot execute .bat wrappers (em++.bat) on Windows.
    Invoke-Emsdk ("emcmake cmake -B $BuildDir -S . " +
        "-G `"NMake Makefiles`" " +
        "-DCMAKE_TOOLCHAIN_FILE=cmake/EmscriptenToolchain.cmake " +
        "-DCMAKE_BUILD_TYPE=Release")

    Write-Ok "Configuration complete."
} else {
    Write-Warn "CMakeCache.txt found — skipping configure. (Delete $BuildDir to reconfigure)"
}

# Build (NMake is single-threaded; no -j flag)
Write-Step "Building target '$Target'..."
Invoke-Emsdk "cmake --build $BuildDir --target $Target"

Write-Ok ""
Write-Ok "Build complete. Output:"
Get-ChildItem "$BuildDir\*.html", "$BuildDir\*.js", "$BuildDir\*.wasm" -ErrorAction SilentlyContinue |
    ForEach-Object { Write-Ok ("  {0,-40} {1,8:N0} KB" -f $_.Name, ($_.Length / 1KB)) }

if ($Command -eq "serve") {
    Write-Step "Starting web server at http://localhost:8000 ..."
    Write-Host "  Press Ctrl+C to stop" -ForegroundColor Yellow
    Write-Host ""

    if ($Open) {
        Start-Process "http://localhost:8000"
    }

    $py = if (Get-Command python3 -ErrorAction SilentlyContinue) { "python3" }
          elseif (Get-Command python -ErrorAction SilentlyContinue) { "python" }
          else { $null }

    if (-not $py) {
        Write-Warn "python not found. Serve manually: cd $BuildDir && python -m http.server 8000"
        exit 0
    }

    Push-Location $BuildDir
    & $py -m http.server 8000
    Pop-Location
}
