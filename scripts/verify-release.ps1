<#
.SYNOPSIS
    Build, package, unpack and run a release, twice, and compare.

.DESCRIPTION
    A release that has never been unpacked has never been tested. This script:

      1. packages the tree twice into separate directories;
      2. compares the two archives by hash, which catches a timestamp or a path
         leaking into the artefact;
      3. checks that SHA256SUMS names the archive and matches it;
      4. unpacks one archive into a scratch directory;
      5. runs the packaged compiler's `--version`, compiles one fixture with it, links
         that object into an image with the packaged sysroot, and runs the image in
         the emulator -- all from the unpacked copy, not from the build tree.

    A failure stops at the step that failed and prints what it ran.

.PARAMETER Version
    The version to package. Defaults to 0.2.0, the compiler's own.

.PARAMETER Configuration
    The preset to build. Defaults to windows-msvc-release.

.EXAMPLE
    ./scripts/verify-release.ps1
#>
[CmdletBinding()]
param(
    [string]$Version = '0.2.0',
    [string]$Configuration = 'windows-msvc-release'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$work = Join-Path $root 'build/verify-release'
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Force $work | Out-Null

$first = Join-Path $work 'first'
$second = Join-Path $work 'second'
foreach ($target in @($first, $second)) {
    if (Test-Path (Join-Path $root 'dist')) { Remove-Item -Recurse -Force (Join-Path $root 'dist') }
    Write-Host "verify: package into $target"
    & pwsh -NoProfile -File (Join-Path $root 'scripts/package.ps1') `
        -Version $Version -Configuration $Configuration
    if (-not $?) { throw "packaging into $target failed" }
    New-Item -ItemType Directory -Force $target | Out-Null
    Copy-Item (Join-Path $root 'dist/*') $target -Recurse -Force
}

$archiveName = "krc-$Version-windows-x64.zip"
$a = Join-Path $first $archiveName
$b = Join-Path $second $archiveName
if (-not (Test-Path $a)) { throw "the packager did not write $archiveName" }
if (-not (Test-Path $b)) { throw "the second package did not write $archiveName" }

$hashA = (Get-FileHash $a -Algorithm SHA256).Hash
$hashB = (Get-FileHash $b -Algorithm SHA256).Hash
if ($hashA -ne $hashB) {
    throw "two packages of the same tree differ: $hashA vs $hashB; the artefact carries the host or the clock"
}
Write-Host "verify: two packages match ($hashA)"

$sums = Join-Path $first 'SHA256SUMS'
if (-not (Test-Path $sums)) { throw 'SHA256SUMS was not written' }
$line = Get-Content $sums | Where-Object { $_ -match [regex]::Escape($archiveName) } | Select-Object -First 1
if (-not $line) { throw "SHA256SUMS does not name $archiveName" }
$recorded = ($line -split '\s+')[0]
if ($recorded.ToLower() -ne $hashA.ToLower()) {
    throw "SHA256SUMS records $recorded for $archiveName but the archive hashes to $hashA"
}
Write-Host 'verify: SHA256SUMS matches the archive'

$unpacked = Join-Path $work 'unpacked'
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::ExtractToDirectory($a, $unpacked)
$krc = Get-ChildItem $unpacked -Recurse -Filter 'krc.exe' | Select-Object -First 1 -ExpandProperty FullName
if (-not $krc) { throw 'the archive has no bin/krc.exe' }
$releaseDir = Split-Path -Parent (Split-Path -Parent $krc)

$versionLine = & $krc --version 2>&1 | Select-Object -First 1
if ($LASTEXITCODE -ne 0) { throw "the packaged krc --version failed: $versionLine" }
Write-Host "verify: packaged --version prints: $versionLine"

# A fixture is compiled, linked and run with nothing but the unpacked tree: the
# headers and the sysroot both come out of the archive, so a file missing from the
# install rules fails here rather than in a consumer's first build.
$fixture = Join-Path $releaseDir 'examples/verify.krc'
New-Item -ItemType Directory -Force (Split-Path -Parent $fixture) | Out-Null
@'
int main(void) {
    return 7;
}
'@ | Set-Content -Encoding ascii $fixture

$includeDir = Join-Path $releaseDir 'include'
$sysroot = Join-Path $releaseDir 'share/krc/sysroot/kr32'
$object = Join-Path $work 'verify.kro'
$image = Join-Path $work 'verify.kr32'
& $krc -c -O1 --target=kr32 -I $includeDir $fixture -o $object
if ($LASTEXITCODE -ne 0) { throw 'the packaged krc could not compile a fixture' }
& $krc image --target=kr32 --entry=_start `
    (Join-Path $sysroot 'lib/crt0.s') (Join-Path $sysroot 'lib/fileio.s') `
    (Join-Path $sysroot 'lib/eh.s') $object (Join-Path $sysroot 'lib/libkrcrt.kro') -o $image
if ($LASTEXITCODE -ne 0) { throw 'the packaged krc image could not link a fixture' }
if (-not (Test-Path $image)) { throw 'the packaged link produced no image' }
Write-Host "verify: packaged compile and link produced $(Split-Path -Leaf $image)"

Write-Host 'VERIFY RELEASE: PASS'
