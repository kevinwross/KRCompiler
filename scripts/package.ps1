<#
.SYNOPSIS
    Build, install and package the compiler and its sysroot for one release.

.DESCRIPTION
    The packaged tree is what a consumer installs: `bin/krc.exe`, `bin/krld.exe`, the
    headers under `include/krc`, and the target sysroots under `share/krc/sysroot`,
    produced by the install rules rather than copied by hand, so the archive cannot
    drift from what `cmake --install` writes.

    The archive is deterministic: entries are sorted by path and stamped with the ZIP
    epoch, so two packages of the same tree are byte-identical and a clock or a host
    path leaking into the artefact is a failure rather than a mystery. `SHA256SUMS`
    is written beside it.

.PARAMETER Version
    The version to name the archive with. Defaults to 0.2.0, the compiler's own.

.PARAMETER Configuration
    The preset to build. Defaults to windows-msvc-release.

.PARAMETER SkipBuild
    Package what is already built. The tests are not run.

.EXAMPLE
    ./scripts/package.ps1
#>
param(
    [string]$Version = '0.2.0',
    [string]$Configuration = 'windows-msvc-release',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    if (-not $SkipBuild) {
        cmake --preset $Configuration
        if ($LASTEXITCODE -ne 0) { throw "cmake --preset $Configuration failed" }
        cmake --build --preset $Configuration
        if ($LASTEXITCODE -ne 0) { throw "cmake --build --preset $Configuration failed" }
        ctest --preset $Configuration
        if ($LASTEXITCODE -ne 0) { throw "ctest --preset $Configuration failed" }
    }

    $dist = Join-Path $root 'dist'
    $name = "krc-$Version-windows-x64"
    $stage = Join-Path $dist $name
    if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
    New-Item -ItemType Directory -Force $dist | Out-Null

    $buildTree = Join-Path $root (Join-Path 'build' $Configuration)
    cmake --install $buildTree --config Release --prefix $stage
    if ($LASTEXITCODE -ne 0) { throw "cmake --install failed" }

    foreach ($file in @('README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md')) {
        $source = Join-Path $root $file
        if (Test-Path $source) { Copy-Item $source (Join-Path $stage $file) }
    }

    $zip = Join-Path $dist "$name.zip"
    if (Test-Path $zip) { Remove-Item -Force $zip }
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $epochSeconds = 315532800  # 1980-01-01, the ZIP epoch
    if ($env:SOURCE_DATE_EPOCH) {
        [int64]$parsed = 0
        if ([int64]::TryParse($env:SOURCE_DATE_EPOCH, [ref]$parsed) -and $parsed -ge 0) {
            $epochSeconds = [Math]::Max([int64]315532800, $parsed)
        }
    }
    $epoch = [DateTimeOffset]::FromUnixTimeSeconds($epochSeconds).UtcDateTime
    $stream = [IO.File]::Open($zip, [IO.FileMode]::Create)
    $archive = [System.IO.Compression.ZipArchive]::new(
        $stream, [System.IO.Compression.ZipArchiveMode]::Create)
    try {
        $files = Get-ChildItem $stage -Recurse -File | Sort-Object FullName
        foreach ($file in $files) {
            $relative = $file.FullName.Substring($dist.Length + 1).Replace('\', '/')
            $entry = $archive.CreateEntry(
                $relative, [System.IO.Compression.CompressionLevel]::Optimal)
            $entry.LastWriteTime = $epoch
            $input = [IO.File]::OpenRead($file.FullName)
            $output = $entry.Open()
            try { $input.CopyTo($output) } finally { $output.Dispose(); $input.Dispose() }
        }
    } finally {
        $archive.Dispose()
        $stream.Dispose()
    }

    $hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $(Split-Path -Leaf $zip)" | Set-Content -Encoding ascii (Join-Path $dist 'SHA256SUMS')
    Write-Host "wrote $zip"
} finally {
    Pop-Location
}
