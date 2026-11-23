# OBS Smooth Zoom - one-command Windows x64 build using the official OBS plugin template.
# Requires Git, CMake >= 3.28, Visual Studio 2022 C++ workload + Windows SDK 10.0.22621.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$template = Join-Path $root '.obs-template-build'
$dist = Join-Path $root 'dist'

function Invoke-Checked([string] $name, [string[]] $arguments) {
    Write-Host ("`n>>> " + $name + ' ' + ($arguments -join ' ')) -ForegroundColor Cyan
    & $name @arguments
    if ($LASTEXITCODE -ne 0) { throw "Command failed ($LASTEXITCODE): $name $($arguments -join ' ')" }
}
function Require-Command([string] $command) {
    if (-not (Get-Command $command -ErrorAction SilentlyContinue)) { throw "Missing command: $command. Install it first and reopen PowerShell." }
}
Require-Command 'git'
Require-Command 'cmake'

# CI runners include VS 2022; a local Windows build also needs the MSVC C++ workload.
if (-not (Test-Path $template)) {
    Invoke-Checked -name 'git' -arguments @('clone', '--depth', '1', 'https://github.com/obsproject/obs-plugintemplate.git', $template)
}
if (-not (Test-Path (Join-Path $template 'CMakeLists.txt'))) {
    throw 'Incomplete template checkout. Delete .obs-template-build and retry.'
}

Copy-Item (Join-Path $root 'src\plugin-main.cpp') (Join-Path $template 'src\plugin-main.cpp') -Force
Copy-Item (Join-Path $root 'src\zoom_math.hpp') (Join-Path $template 'src\zoom_math.hpp') -Force
Copy-Item (Join-Path $root 'data\zoom.effect') (Join-Path $template 'data\zoom.effect') -Force

# Patch the upstream template in the generated BUILD copy only.
$cmakePath = Join-Path $template 'CMakeLists.txt'
$cmake = [System.IO.File]::ReadAllText($cmakePath)
$needle = 'target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-main.c)'
if (-not $cmake.Contains($needle)) { throw 'Upstream OBS template changed CMakeLists.txt; integration needs updating.' }
$cmake = $cmake.Replace($needle, @'
target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-main.cpp src/zoom_math.hpp)
if(MSVC)
  target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE /utf-8)
endif()
install(FILES data/zoom.effect DESTINATION "data/obs-plugins/obs-smooth-zoom")
'@)
[System.IO.File]::WriteAllText($cmakePath, $cmake, (New-Object System.Text.UTF8Encoding($false)))

$buildspecPath = Join-Path $template 'buildspec.json'
$spec = Get-Content $buildspecPath -Raw | ConvertFrom-Json
$spec.name = 'obs-smooth-zoom'
$spec.displayName = 'OBS Smooth Zoom'
$spec.version = '0.1.0'
$spec.author = 'OBS Smooth Zoom contributors'
$spec.website = 'https://obsproject.com/'
$spec.email = 'none@example.invalid'
[System.IO.File]::WriteAllText($buildspecPath, ($spec | ConvertTo-Json -Depth 40), (New-Object System.Text.UTF8Encoding($false)))

Push-Location $template
try {
    Invoke-Checked -name 'cmake' -arguments @('--preset', 'windows-x64', '-DENABLE_FRONTEND_API:BOOL=ON', '-DENABLE_QT:BOOL=ON')
    Invoke-Checked -name 'cmake' -arguments @('--build', '--preset', 'windows-x64', '--config', 'Release', '--parallel')
} finally {
    Pop-Location
}

# Locate the actual module; fail instead of shipping an empty/incorrect ZIP.
$dlls = @(Get-ChildItem (Join-Path $template 'build_x64') -Filter 'obs-smooth-zoom.dll' -File -Recurse)
if ($dlls.Count -eq 0) { throw 'Build finished but obs-smooth-zoom.dll not found.' }
$dll = $dlls | Where-Object { $_.FullName -match '[\\/]Release[\\/]' } | Select-Object -First 1
if (-not $dll) { $dll = $dlls | Select-Object -First 1 }

$package = Join-Path $dist 'package'
if (Test-Path $package) { Remove-Item $package -Recurse -Force }
$dllDir = Join-Path $package 'obs-plugins\64bit'
$shaderDir = Join-Path $package 'data\obs-plugins\obs-smooth-zoom'
New-Item -ItemType Directory -Force -Path $dllDir, $shaderDir | Out-Null
Copy-Item $dll.FullName (Join-Path $dllDir 'obs-smooth-zoom.dll') -Force
Copy-Item (Join-Path $root 'data\zoom.effect') (Join-Path $shaderDir 'zoom.effect') -Force
$zip = Join-Path $dist 'obs-smooth-zoom-Windows-x64.zip'
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $package '*') -DestinationPath $zip -Force
Write-Host "`nSUCCESS: $zip" -ForegroundColor Green
Write-Host 'This ZIP contains the plugin DLL and shader. Test it on a separate OBS scene before using it live.'
