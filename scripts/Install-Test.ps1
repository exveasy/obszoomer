# Installs the freshly built DLL and shader to the standard OBS Studio folder.
# Run as an Administrator after closing OBS. Existing plugin files are backed up.
[CmdletBinding()]
param([string]$ObsPath = 'C:\Program Files\obs-studio')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$package = Join-Path $root 'dist\package'
$dll = Join-Path $package 'obs-plugins\64bit\obs-smooth-zoom.dll'
$shader = Join-Path $package 'data\obs-plugins\obs-smooth-zoom\zoom.effect'
if (-not (Test-Path $dll) -or -not (Test-Path $shader)) {
    throw 'Built files are missing. Run scripts\Build-Easy-Windows.ps1 first, or extract the build artifact ZIP to dist\package.'
}
if (-not (Test-Path (Join-Path $ObsPath 'bin\64bit\obs64.exe'))) {
    throw "OBS not found at $ObsPath. Pass -ObsPath 'D:\path\to\obs-studio'."
}
if (Get-Process -Name 'obs64' -ErrorAction SilentlyContinue) {
    throw 'OBS Studio is running. Close it before copying plugin files.'
}
$dstDll = Join-Path $ObsPath 'obs-plugins\64bit\obs-smooth-zoom.dll'
$dstShader = Join-Path $ObsPath 'data\obs-plugins\obs-smooth-zoom\zoom.effect'
foreach ($pair in @(@($dll,$dstDll), @($shader,$dstShader))) {
    $destinationDir = Split-Path $pair[1] -Parent
    New-Item -ItemType Directory -Force -Path $destinationDir | Out-Null
    if (Test-Path $pair[1]) { Copy-Item $pair[1] "$($pair[1]).bak" -Force }
    Copy-Item $pair[0] $pair[1] -Force
}
Write-Host 'Installed OBS Smooth Zoom. Start OBS > Tools > OBS Smooth Zoom...' -ForegroundColor Green
