<#
.SYNOPSIS
    Copies the family's binaries into one directory, so they can sit on PATH.

.DESCRIPTION
    The tools a person runs are spread across six repositories and several build
    directories: krc and krld under krcompiler, one machine binary per VM, the
    disassemblers and bench in kr216 and kr232, and two programs in krdbg. This script
    finds each of them for one configuration and copies them into a single directory.

    kr32.exe also gets the SDL2.dll it links, copied beside it: Windows resolves that
    DLL from the executable's own directory, so a directory holding the executable
    alone contains a kr32 that cannot start.

    It reports what it found and what it did not, and exits non-zero when anything is
    missing: a partial PATH directory that looks complete is the failure this is meant
    to prevent. The directory is checked against PATH and the exact line needed is
    printed when it is not there yet.

.PARAMETER Dest
    Where to copy them. Defaults to "$HOME/.local/bin", which is on PATH on this
    machine. Any directory works; if it is not on PATH the script says so.

.PARAMETER Config
    Debug (the default) or Release. Each repository's debug and release presets are
    used, so the directories differ between the two.

.PARAMETER AllowMissing
    Copy what exists and exit 0 even when something does not. The summary still names
    it. Without this, a missing binary is an error.

.EXAMPLE
    ./scripts/install-tools.ps1
    ./scripts/install-tools.ps1 -Dest C:\Users\kevin\bin
    ./scripts/install-tools.ps1 -Config Release
#>
[CmdletBinding()]
param(
    [string]$Dest = '',
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Debug',
    [switch]$AllowMissing
)

$ErrorActionPreference = 'Stop'

# <repo>/scripts, so the family root is two levels up.
$family = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Dest) { $Dest = Join-Path $HOME '.local/bin' }
$Dest = [System.IO.Path]::GetFullPath($Dest)

# One row per binary: the name it takes on PATH, its repository, and the build
# directory it comes from for each configuration. The paths are the presets' own
# binaryDir values; a preset that moves breaks this, which is the point of writing
# them down once instead of in a dozen shell histories.
$debug = [ordered]@{
    'krc.exe'            = 'krcompiler/build/windows-msvc-debug/host/bin/Debug'
    'krld.exe'           = 'krcompiler/build/windows-msvc-debug/host/bin/Debug'
    'kr32.exe'           = 'kr32/build/windows-msvc/Debug'
    'kr16.exe'           = 'kr16/build/windows-ninja'
    'kr216.exe'          = 'kr216/build/windows-msvc-debug/tools/kr216-emu/Debug'
    'kr216-dis.exe'      = 'kr216/build/windows-msvc-debug/tools/kr216-dis/Debug'
    'kr216-bench.exe'    = 'kr216/build/windows-msvc-debug/tools/kr216-bench/Debug'
    'kr232.exe'          = 'kr232/build/windows-msvc'
    'kr232-dis.exe'      = 'kr232/build/windows-msvc'
    'krdbg_demo.exe'     = 'krdbg/build/windows-msvc'
    'krdbg_settings.exe' = 'krdbg/build/windows-msvc'
}
$release = [ordered]@{
    'krc.exe'            = 'krcompiler/build/windows-msvc-release/host/bin/Release'
    'krld.exe'           = 'krcompiler/build/windows-msvc-release/host/bin/Release'
    'kr32.exe'           = 'kr32/build/windows-msvc-release/Release'
    'kr16.exe'           = 'kr16/build/windows-ninja-release'
    'kr216.exe'          = 'kr216/build/windows-msvc-release/tools/kr216-emu/Release'
    'kr216-dis.exe'      = 'kr216/build/windows-msvc-release/tools/kr216-dis/Release'
    'kr216-bench.exe'    = 'kr216/build/windows-msvc-release/tools/kr216-bench/Release'
    'kr232.exe'          = 'kr232/build/windows-msvc-release'
    'kr232-dis.exe'      = 'kr232/build/windows-msvc-release'
    'krdbg_demo.exe'     = 'krdbg/build/windows-msvc-release'
    'krdbg_settings.exe' = 'krdbg/build/windows-msvc-release'
}
$layout = if ($Config -eq 'Release') { $release } else { $debug }

New-Item -ItemType Directory -Force $Dest | Out-Null

$copied = @()
$missing = @()
foreach ($name in $layout.Keys) {
    $source = Join-Path (Join-Path $family $layout[$name]) $name
    if (-not (Test-Path -LiteralPath $source)) {
        $missing += [pscustomobject]@{ Name = $name; Expected = $source }
        continue
    }
    $target = Join-Path $Dest $name
    Copy-Item -LiteralPath $source -Destination $target -Force
    $copied += [pscustomobject]@{
        Name = $name
        Size = (Get-Item -LiteralPath $target).Length
    }
}

# kr32.exe links SDL2 dynamically and Windows loads the DLL from beside the
# executable, so copying the executable alone leaves a kr32 that cannot start --
# the complete-looking directory this script exists to prevent. The DLL is taken
# from the same build tree as the executable it belongs to.
$sdl2Source = Join-Path (Join-Path $family $layout['kr32.exe']) 'SDL2.dll'
$sdl2 = $null
if ((Test-Path -LiteralPath (Join-Path $Dest 'kr32.exe')) -and
    (Test-Path -LiteralPath $sdl2Source)) {
    $sdl2Target = Join-Path $Dest 'SDL2.dll'
    Copy-Item -LiteralPath $sdl2Source -Destination $sdl2Target -Force
    $sdl2 = [pscustomobject]@{
        Name = 'SDL2.dll'
        Size = (Get-Item -LiteralPath $sdl2Target).Length
    }
}
$noRuntime = -not $sdl2 -and (Test-Path -LiteralPath (Join-Path $Dest 'kr32.exe'))

Write-Host ''
Write-Host "install-tools: $($Config) into $Dest"
foreach ($row in $copied) {
    Write-Host ("  copied  {0,-20} {1,9:N0} bytes" -f $row.Name, $row.Size)
}
if ($sdl2) {
    Write-Host ("  copied  {0,-20} {1,9:N0} bytes  (kr32.exe runtime)" -f $sdl2.Name, $sdl2.Size)
}
foreach ($row in $missing) {
    Write-Host ("  MISSING {0,-20} {1}" -f $row.Name, $row.Expected) -ForegroundColor Red
}
if ($noRuntime) {
    Write-Host ("  MISSING {0,-20} {1}" -f 'SDL2.dll', $sdl2Source) -ForegroundColor Red
}

# The directory has to be on PATH, or the copy has changed nothing about how a shell
# finds the tools.
$onPath = ($env:PATH -split ';') | Where-Object {
    $_ -and [System.IO.Path]::GetFullPath($_).TrimEnd('\') -eq $Dest.TrimEnd('\')
}
Write-Host ''
if ($onPath) {
    Write-Host "  $Dest is on PATH"
} else {
    Write-Host "  $Dest is NOT on PATH. Add it with:" -ForegroundColor Yellow
    Write-Host "    [Environment]::SetEnvironmentVariable('PATH', [Environment]::GetEnvironmentVariable('PATH','User') + ';$Dest', 'User')"
    Write-Host '  then open a new shell.'
}

if ($missing.Count -gt 0) {
    Write-Host ''
    Write-Host "  $($missing.Count) of $($layout.Count) binaries are missing; build their trees first:" -ForegroundColor Yellow
    $repos = $missing | ForEach-Object { ($_.Expected -split '[\\/]')[1] } | Sort-Object -Unique
    foreach ($repo in $repos) {
        $preset = if ($Config -eq 'Release') {
            if ($repo -eq 'kr16') { 'windows-ninja-release' } else { 'windows-msvc-release' }
        } else {
            if ($repo -eq 'kr16') { 'windows-ninja-debug' } else { 'windows-msvc-debug' }
        }
        Write-Host "    (cd $repo; cmake --preset $preset; cmake --build --preset $preset)"
    }
    if (-not $AllowMissing) { exit 1 }
}

if ($noRuntime) {
    Write-Host ''
    Write-Host '  kr32.exe was copied without the SDL2.dll it links, so it will not start.' -ForegroundColor Red
    if (-not $AllowMissing) { exit 1 }
}
