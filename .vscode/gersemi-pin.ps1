# Shared helper: resolves gersemi pinned to the exact version installed by CI
# (.github/actions/run-gersemi -> `brew install obsproject/tools/gersemi`).
#
# Different gersemi versions can disagree on formatting (e.g. `list_expansion`
# support was added after 0.21.0), so any local check/format that should match
# CI must use this pinned version rather than whatever `gersemi` is on PATH.
#
# Dot-source this file, then call Get-PinnedGersemi.

$GersemiPinnedVersion = '0.21.0'

function Get-PinnedGersemi {
    param(
        [Parameter(Mandatory)]
        [string] $ProjectRoot
    )

    $InstallDir = Join-Path $ProjectRoot ".deps/gersemi-ci-$GersemiPinnedVersion"

    $PythonExe = $null
    $PythonBaseArgs = @()

    $Py = Get-Command py -ErrorAction SilentlyContinue
    if ( $Py ) {
        & $Py.Source -3.12 --version *> $null
        if ( $LASTEXITCODE -eq 0 ) {
            $PythonExe = $Py.Source
            $PythonBaseArgs = @('-3.12')
        }
    }

    if ( -not $PythonExe ) {
        $Python = Get-Command python3, python -ErrorAction SilentlyContinue | Select-Object -First 1
        if ( -not $Python ) {
            throw 'Python 3.12 was not found. Install it, or add "py -3.12" / "python3" to PATH.'
        }
        $PythonExe = $Python.Source
    }

    if ( -not (Test-Path (Join-Path $InstallDir "gersemi-$GersemiPinnedVersion.dist-info")) ) {
        Write-Host "Installing gersemi $GersemiPinnedVersion (pinned to match CI) into .deps\gersemi-ci-$GersemiPinnedVersion..." -ForegroundColor Yellow
        & $PythonExe @($PythonBaseArgs + @('-m', 'pip', 'install', '--quiet', '--target', $InstallDir, "gersemi==$GersemiPinnedVersion"))
        if ( $LASTEXITCODE -ne 0 ) {
            throw "Failed to install gersemi $GersemiPinnedVersion."
        }
    }

    $env:PYTHONPATH = $InstallDir

    return [PSCustomObject]@{
        Exe      = $PythonExe
        BaseArgs = $PythonBaseArgs + @('-m', 'gersemi')
        Version  = $GersemiPinnedVersion
    }
}
