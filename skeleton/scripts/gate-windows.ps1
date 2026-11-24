<#
.SYNOPSIS
    Builds and tests a VM tree, with the job count the family gate hands down.

.DESCRIPTION
    The same gate every VM tree has: the family passes FAMILY_JOBS so several
    repositories can share one machine without oversubscribing it, and defaults are
    computed from the core count when the script is run by hand.

.PARAMETER Preset
    The preset to build and test. Defaults to the host's debug preset.
#>
[CmdletBinding()]
param([string]$Preset = '')

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

if (-not $Preset) {
    $Preset = if ($IsWindows) { 'windows-msvc-debug' } else { 'linux-gcc-debug' }
}

$cores = if ($env:NUMBER_OF_PROCESSORS) { [int]$env:NUMBER_OF_PROCESSORS }
         else { [Environment]::ProcessorCount }
$jobs = if ($env:FAMILY_JOBS) { [Math]::Max(1, [int]$env:FAMILY_JOBS) }
        else { [Math]::Max(1, $cores) }
Write-Host "gate: $Preset, $cores logical processors -> $jobs jobs"

cmake --preset $Preset
if ($LASTEXITCODE -ne 0) { throw "configure failed" }
cmake --build --preset $Preset -j $jobs
if ($LASTEXITCODE -ne 0) { throw "build failed" }
ctest --preset $Preset -j $jobs
if ($LASTEXITCODE -ne 0) { throw "tests failed" }
Write-Host "GATE: PASS ($Preset)"
