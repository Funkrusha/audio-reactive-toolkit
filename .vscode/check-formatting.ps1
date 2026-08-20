[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$ProjectRoot = Resolve-Path "$PSScriptRoot/.."

function Find-ClangFormat {
    $Command = Get-Command clang-format-19, clang-format -ErrorAction SilentlyContinue | Select-Object -First 1
    if ( $Command ) {
        return $Command.Source
    }

    $VisualStudioRoot = Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio'
    $Candidates = Get-ChildItem -Path $VisualStudioRoot -Filter clang-format.exe -Recurse -ErrorAction SilentlyContinue |
        Where-Object FullName -Match '\\VC\\Tools\\Llvm\\x64\\bin\\clang-format\.exe$' |
        Sort-Object FullName -Descending

    if ( $Candidates ) {
        return $Candidates[0].FullName
    }

    throw 'clang-format was not found. Install the LLVM tools with Visual Studio or add clang-format 19 to PATH.'
}

function Get-TrackedFiles([string[]] $Patterns) {
    $Files = git -C $ProjectRoot ls-files -- $Patterns
    if ( $LASTEXITCODE -ne 0 ) {
        throw 'Unable to list tracked source files with Git.'
    }
    return $Files
}

$ClangFormat = Find-ClangFormat

# Pinned to the exact version CI installs, rather than whatever `gersemi` is
# on PATH -- different versions disagree on formatting (see gersemi-pin.ps1).
. "$PSScriptRoot\gersemi-pin.ps1"
$Gersemi = Get-PinnedGersemi -ProjectRoot $ProjectRoot
$GersemiExecutable = $Gersemi.Exe
$GersemiArguments = $Gersemi.BaseArgs

$SourceFiles = Get-TrackedFiles @('*.c', '*.h', '*.cpp', '*.hpp', '*.m', '*.mm')
$CMakeFiles = Get-TrackedFiles @('CMakeLists.txt', '*.cmake')

Write-Host "Checking $($SourceFiles.Count) source files with clang-format..."
if ( $SourceFiles ) {
    & $ClangFormat --dry-run --Werror --style=file --fallback-style=none $SourceFiles
    if ( $LASTEXITCODE -ne 0 ) {
        throw 'clang-format check failed.'
    }
}

Write-Host "Checking $($CMakeFiles.Count) CMake files with gersemi..."
if ( $CMakeFiles ) {
    & $GersemiExecutable @GersemiArguments --check --no-cache $CMakeFiles
    if ( $LASTEXITCODE -ne 0 ) {
        Write-Host 'gersemi proposed changes:' -ForegroundColor Yellow
        & $GersemiExecutable @GersemiArguments --diff --no-cache $CMakeFiles
        throw 'gersemi check failed.'
    }
}

Write-Host 'Formatting checks passed.' -ForegroundColor Green
