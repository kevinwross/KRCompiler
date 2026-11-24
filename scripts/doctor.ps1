<#
.SYNOPSIS
    Report the tools a repository's Windows gate needs, with the version each
    one resolved to, and stop before the first build if one is missing.

.DESCRIPTION
    The counterpart of scripts/doctor.sh for the Windows gates (J5). The value is
    not the list: it is that a missing tool fails with a sentence naming the tool
    and the repository that needs it, instead of a compiler error twenty lines
    into a build.

.PARAMETER Repo
    The repository name, used in the messages.

.PARAMETER Need
    Extra tools, comma separated, e.g. "krc,krld".

.EXAMPLE
    ./scripts/doctor.ps1 -Repo kr232 -Need krc,krld
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Repo,
    [string[]]$Need = @()
)

$ErrorActionPreference = 'Stop'
$missing = @()

function Show-Tool([string]$Name, [string]$Why, [switch]$Optional) {
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if (-not $command) {
        if ($Optional) {
            Write-Host ("  {0,-10} {1}" -f $Name, 'not on PATH (optional here)')
            return
        }
        Write-Host "doctor: $Repo needs '$Name' ($Why) and it is not on PATH" -ForegroundColor Red
        $script:missing += $Name
        return
    }
    $version = ''
    try {
        $version = (& $command.Source --version 2>$null | Select-Object -First 1)
    } catch {
        $version = ''
    }
    if (-not $version) { $version = 'unknown version' }
    Write-Host ("  {0,-10} {1,-40} {2}" -f $Name, $version.Trim(), $command.Source)
}

Write-Host "doctor: $Repo"
Show-Tool 'cmake' 'configuring the build'
Show-Tool 'git' 'the tree the gate is checking'
Show-Tool 'node' 'the spec lint' -Optional
# `cl` is provided by vcvarsall, not by PATH, so a gate started from a plain shell
# has no compiler yet and that is not a repository's fault.
Show-Tool 'cl' 'compiling' -Optional
# cmd passes "-Need krc,krld" as one string, so each element is split as well.
foreach ($entry in $Need) {
    if ([string]::IsNullOrWhiteSpace($entry)) { continue }
    foreach ($tool in ($entry -split ',')) {
        if ([string]::IsNullOrWhiteSpace($tool)) { continue }
        Show-Tool $tool.Trim() 'building guest code'
    }
}

if ($missing.Count -ne 0) {
    Write-Host "doctor: $Repo is missing $($missing.Count) tool(s); nothing was built" -ForegroundColor Red
    exit 1
}
Write-Host "doctor: $Repo has every tool it needs"
