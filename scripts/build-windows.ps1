# Build a Windows x64 plugin. Requires Visual Studio C++ workload, CMake, Qt6 and built OBS SDK.
# Example: .\scripts\build-windows.ps1 -ObsSdk 'C:\obs-build' -QtPrefix 'C:\Qt\6.8.3\msvc2022_64'
param(
    [Parameter(Mandatory = $true)][string]$ObsSdk,
    [Parameter(Mandatory = $true)][string]$QtPrefix
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (!(Test-Path $ObsSdk)) { throw "OBS SDK path not found: $ObsSdk" }
if (!(Test-Path $QtPrefix)) { throw "Qt path not found: $QtPrefix" }
Push-Location $root
try {
    cmake -S . -B build-x64 -G 'Visual Studio 17 2022' -A x64 `
        "-DOBS_BUILD_DIR=$ObsSdk" "-DCMAKE_PREFIX_PATH=$QtPrefix" `
        '-DCMAKE_INSTALL_PREFIX=stage'
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
    cmake --build build-x64 --config Release
    if ($LASTEXITCODE -ne 0) { throw 'CMake build failed' }
    cmake --install build-x64 --config Release --prefix "$root\stage"
    if ($LASTEXITCODE -ne 0) { throw 'CMake install failed' }
    $zip = Join-Path $root 'obs-smooth-zoom-Windows-x64.zip'
    if (Test-Path $zip) { Remove-Item $zip }
    Compress-Archive -Path "$root\stage\*" -DestinationPath $zip -Force
    Write-Host "Package ready: $zip"
} finally { Pop-Location }
