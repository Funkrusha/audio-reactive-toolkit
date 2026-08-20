[CmdletBinding()]
param(
    [string]$Configuration = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Resolve-Path "$PSScriptRoot/.."
$BuildDirectory = Join-Path $ProjectRoot 'build_x64'
$InstallPrefix = 'C:/ProgramData/obs-studio/plugins'

$CMake = Get-Command cmake, cmake.exe -ErrorAction SilentlyContinue | Select-Object -First 1
if ( $CMake ) {
    $CMakeExecutable = $CMake.Source
} else {
    $VisualStudioRoot = Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio'
    $Candidates = Get-ChildItem -Path $VisualStudioRoot -Filter cmake.exe -Recurse -ErrorAction SilentlyContinue |
        Where-Object FullName -Match '\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin\\cmake\.exe$' |
        Sort-Object FullName -Descending

    if ( ! $Candidates ) {
        throw 'cmake.exe was not found. Install CMake Tools with Visual Studio or add CMake to PATH.'
    }
    $CMakeExecutable = $Candidates[0].FullName
}

if ( ! ( Test-Path ( Join-Path $BuildDirectory 'CMakeCache.txt' ) ) ) {
    throw 'build_x64 is not configured. Run the ART Windows build task first.'
}

& $CMakeExecutable --install $BuildDirectory --config $Configuration --prefix $InstallPrefix
if ( $LASTEXITCODE -ne 0 ) {
    throw 'ART installation failed. Close OBS and retry from an elevated VS Code window if access was denied.'
}

Write-Host "ART ($Configuration) installed to $InstallPrefix/audio-reactive-toolkit" -ForegroundColor Green
