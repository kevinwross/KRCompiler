<#
.SYNOPSIS
    Configure, build and install KRCompiler into a named prefix.

.DESCRIPTION
    The bootstrap for a machine that has no compiler: it takes a source tree and
    a prefix, and leaves behind a working `krc` whose `--version` reports the
    build it came from. It stops at the first failure and says what failed,
    because a half-installed prefix is worse than none -- the two prefixes this
    project has been bitten by were both "installed" and neither could be trusted.

    It does not install CMake, a compiler or a generator. Those are the machine's
    business; this script only refuses to run without them.

.PARAMETER Prefix
    Where to install. Required: a default would scatter prefixes the same way
    the ad-hoc installs did.

.PARAMETER Config
    Debug (default) or Release. The prefix records which one it is in its stamp.

.PARAMETER BuildDir
    Build tree, default build/bootstrap.

.EXAMPLE
    ./scripts/bootstrap.ps1 -Prefix C:/krc
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Prefix,
    [ValidateSet('Debug', 'Release')][string]$Config = 'Debug',
    [string]$BuildDir = 'build/bootstrap'
)

$ErrorActionPreference = 'Stop'

function Require-Tool([string]$Name) {
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if (-not $command) {
        throw "$Name was not found on PATH; bootstrap installs the compiler, not its tools"
    }
    return $command.Source
}

$cmake = Require-Tool 'cmake'
$root = Split-Path -Parent $PSScriptRoot
Write-Host "bootstrap: $root -> $Prefix ($Config)"

# The Visual Studio generator needs no cl on PATH, which is the state a bare
# machine is in; a Ninja configure here would fail before anything is built.
& $cmake -S $root -B (Join-Path $root $BuildDir) -G 'Visual Studio 18 2026' -A x64
if ($LASTEXITCODE -ne 0) { throw "configure failed ($LASTEXITCODE)" }

& $cmake --build (Join-Path $root $BuildDir) --config $Config
if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }

& $cmake --install (Join-Path $root $BuildDir) --config $Config --prefix $Prefix
if ($LASTEXITCODE -ne 0) { throw "install failed ($LASTEXITCODE)" }

$krc = Join-Path $Prefix "bin/krc.exe"
if (-not (Test-Path $krc)) { throw "install reported success but $krc does not exist" }
$version = & $krc --version
if ($LASTEXITCODE -ne 0) { throw "$krc --version failed ($LASTEXITCODE)" }
Write-Host ($version -join "`n")

$stamp = Join-Path $Prefix "lib/cmake/krc/krc-stamp.cmake"
if (-not (Test-Path $stamp)) { throw "no stamp at $stamp; the prefix cannot describe itself" }
Write-Host "bootstrap: done, prefix describes itself as:"
Get-Content $stamp | Write-Host
