# The krcompiler gate: every Windows configuration this host can reach.
#
# The repository's checks are presets, and AGENTS.local.md requires the matrix
# when a change crosses a boundary. This is that matrix as one command: configure,
# build and test each preset in turn, then run the unit binary from each tree,
# because `krc_tests` is not a ctest row.
#
# A failing preset does not stop the others: the whole picture is more useful than
# the first error, and the exit code tells the caller whether anything failed.
#
# Usage: pwsh -File scripts/gate-windows.ps1 [-Preset <name>] [-SkipTests]
param(
    [string]$Preset = '',
    [switch]$SkipTests
)
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# CMake and ctest are not on PATH on this machine; the Visual Studio copy is. Fall
# back to the bare names for a machine where they are on PATH.
$cmake = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
if (-not (Test-Path $cmake)) { $cmake = 'cmake'; $ctest = 'ctest' }

# The job count lives here because a test preset cannot compute one; the same rule
# is in scripts/gate-linux.sh.
$cores = if ($env:NUMBER_OF_PROCESSORS) { [int]$env:NUMBER_OF_PROCESSORS } else { [Environment]::ProcessorCount }
$jobs = [Math]::Max(1, [Math]::Floor($cores * 0.8))

$presets = if ($Preset) { @($Preset) } else { @('windows-msvc-debug', 'windows-msvc-release') }
$failed = 0

# The label and status go to the host stream, not the pipeline: a caller that
# writes `if (Step ...)` captures everything the function emits, which is how the
# first version of this gate ran every check and printed none of them.
$script:ok = $false
function Step([string]$label, [scriptblock]$action) {
    Write-Host "== $label"
    & $action
    $script:ok = ($LASTEXITCODE -eq 0)
    if (-not $script:ok) {
        Write-Host "   FAIL: $label (exit $LASTEXITCODE)"
        $script:failed = 1
    }
}

foreach ($name in $presets) {
    Write-Output ''
    Write-Output "== preset $name"
    Step "$name configure" { & $cmake --preset $name }
    if (-not $script:ok) { continue }
    Step "$name build" { & $cmake --build --preset $name --parallel $jobs }
    if (-not $script:ok) { continue }
    if (-not $SkipTests) {
        Step "$name ctest" { & $ctest --preset $name --output-on-failure --no-tests=error }
        if (-not $script:ok) { continue }
        # krc_tests is a binary, not a ctest row.
        $config = if ($name -match 'release') { 'Release' } else { 'Debug' }
        $unit = Join-Path $root "build/$name/host/bin/$config/krc_tests.exe"
        if (Test-Path $unit) {
            Step "$name krc_tests" { & $unit } | Out-Null
        } else {
            Write-Output "   FAIL: $name wrote no krc_tests at $unit"
            $failed = 1
        }
    }
}

Write-Output ''
if ($failed -ne 0) {
    Write-Output 'GATE WINDOWS: FAIL'
    exit 1
}
Write-Output 'GATE WINDOWS: PASS'
