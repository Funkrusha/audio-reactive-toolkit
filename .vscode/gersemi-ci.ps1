[CmdletBinding()]
param(
    [ValidateSet('Check', 'Format')]
    [string] $Mode = 'Check'
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Resolve-Path "$PSScriptRoot/.."

. "$PSScriptRoot\gersemi-pin.ps1"
$Python = Get-PinnedGersemi -ProjectRoot $ProjectRoot
$GersemiVersion = $Python.Version

function Invoke-Python([string[]] $Arguments) {
    & $Python.Exe @($Python.BaseArgs + $Arguments)
}

function Get-TrackedFiles([string[]] $Patterns) {
    $Files = git -C $ProjectRoot ls-files -- $Patterns
    if ( $LASTEXITCODE -ne 0 ) {
        throw 'Unable to list tracked source files with Git.'
    }
    return $Files
}

$CMakeFiles = Get-TrackedFiles @('CMakeLists.txt', '*.cmake')

if ( -not $CMakeFiles ) {
    Write-Host 'No CMake files found.' -ForegroundColor Yellow
    exit 0
}

if ( $Mode -eq 'Check' ) {
    Write-Host "Checking $($CMakeFiles.Count) CMake files with gersemi $GersemiVersion (CI-pinned)..."
    Invoke-Python (@('--check', '--no-cache') + $CMakeFiles)
    if ( $LASTEXITCODE -ne 0 ) {
        Write-Host 'gersemi proposed changes:' -ForegroundColor Yellow
        Invoke-Python (@('--diff', '--no-cache') + $CMakeFiles)
        throw 'gersemi check failed.'
    }
    Write-Host 'gersemi check passed.' -ForegroundColor Green
} else {
    Write-Host "Formatting $($CMakeFiles.Count) CMake files with gersemi $GersemiVersion (CI-pinned)..."
    Invoke-Python (@('-i') + $CMakeFiles)
    if ( $LASTEXITCODE -ne 0 ) {
        throw 'gersemi formatting failed.'
    }
    Write-Host 'gersemi formatting applied.' -ForegroundColor Green
}
