#!/usr/bin/env pwsh
#Requires -Version 5.1
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo
# Manually refresh the vendored Ultimate ASI Loader. See AGENTS.md "Vendoring".
#
# Ultimate-ASI-Loader ships its DLL inside a release zip rather than as a
# standalone asset, so this extracts dinput8.dll instead of calling
# Update-VendoredLoader, which vendors the downloaded artifact whole.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

Import-Module (Join-Path $PSScriptRoot 'ModProject.psm1') -Force
$project = Get-ModProject
Import-Module (Join-Path $project.Root 'cameraunlock-core/powershell/ModLoaderSetup.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'ReleaseSupport.psm1') -Force

# Fixed by the PE format: the offset of the PE header pointer in the DOS stub,
# the "PE\0\0" signature it points at, and IMAGE_FILE_MACHINE_AMD64. A linker's
# DOS stub puts the PE header a few hundred bytes in, so a 4 KB prefix always
# covers it.
$PeHeaderPointerOffset = 0x3C
$PeSignature           = 0x00004550
$ImageFileMachineAmd64 = 0x8664
$PeHeaderSearchBytes   = 0x1000

$vendorDir     = $project.VendorLoaderDir
$vendorDll     = $project.VendorLoaderDll
$vendorLicense = Join-Path $vendorDir 'LICENSE'
$vendorReadme  = Join-Path $vendorDir 'README.md'
if (-not (Test-Path -LiteralPath $vendorDir)) {
    New-Item -ItemType Directory -Path $vendorDir -Force | Out-Null
}

# install.cmd owns the proxy filename; reading it back keeps this README
# describing the file the installer actually deploys.
$proxyName = Get-AsiLoaderProxyName -InstallCmdPath $project.InstallCmdPath

$tempDir = Join-Path $env:TEMP ("asi-update-" + [IO.Path]::GetRandomFileName())
New-Item -ItemType Directory -Path $tempDir -Force | Out-Null
$tempZip     = Join-Path $tempDir 'upstream.zip'
$tempDll     = Join-Path $tempDir 'dinput8.dll'
$tempLicense = Join-Path $tempDir 'LICENSE'
try {
    Write-Host "Refreshing vendor/ultimate-asi-loader from upstream..." -ForegroundColor Cyan

    # TheGreatCircle.exe is a 64-bit image, so the x64 asset is the only one
    # that can be loaded into it; an x86 proxy in an x64 game's exe directory
    # crashes it on launch before our code runs.
    $meta = Invoke-FetchLatestLoader `
        -OutputPath $tempZip `
        -Owner 'ThirteenAG' -Repo 'Ultimate-ASI-Loader' `
        -VersionPrefix 'v9.' `
        -AssetPattern '^Ultimate-ASI-Loader_x64\.zip$'

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($tempZip)
    try {
        $dllEntry = $zip.Entries | Where-Object { $_.Name -ieq 'dinput8.dll' } | Select-Object -First 1
        if (-not $dllEntry) { throw "Upstream zip $($meta.AssetName) does not contain dinput8.dll." }
        [System.IO.Compression.ZipFileExtensions]::ExtractToFile($dllEntry, $tempDll, $true)

        $licenseEntry = $zip.Entries | Where-Object { $_.Name -match '^(license|LICENSE)(\..+)?$' -and $_.FullName -notmatch '/.+/' } | Select-Object -First 1
        if ($licenseEntry) {
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($licenseEntry, $tempLicense, $true)
        }
    } finally { $zip.Dispose() }

    # Vetted in the temp directory, before anything is moved into vendor/.
    # Writing straight into vendor/ leaves an unvetted binary - a truncated
    # entry, or an x86 proxy - sitting at the exact path install.cmd and
    # deploy.ps1 ship from whenever a check below fails.
    $header = New-Object byte[] $PeHeaderSearchBytes
    $stream = [System.IO.File]::OpenRead($tempDll)
    try {
        # Looped, because FileStream.Read may return fewer bytes than asked for.
        # Treating one short read as end-of-file rejects a sound loader with a
        # "no PE header" message that sends the next session hunting upstream.
        $headerLength = 0
        while ($headerLength -lt $header.Length) {
            $read = $stream.Read($header, $headerLength, $header.Length - $headerLength)
            if ($read -le 0) { break }
            $headerLength += $read
        }
    } finally { $stream.Dispose() }

    $peOffset = if ($headerLength -ge ($PeHeaderPointerOffset + 4)) {
        [BitConverter]::ToInt32($header, $PeHeaderPointerOffset)
    } else {
        -1
    }
    if ($peOffset -lt 0 -or ($peOffset + 6) -gt $headerLength) {
        throw "Extracted ASI Loader has no PE header within its first $PeHeaderSearchBytes bytes; refusing to vendor it."
    }
    if ([BitConverter]::ToUInt32($header, $peOffset) -ne $PeSignature) {
        throw "Extracted ASI Loader is not a PE image; refusing to vendor it."
    }
    $machine = [BitConverter]::ToUInt16($header, $peOffset + 4)
    if ($machine -ne $ImageFileMachineAmd64) {
        throw ("Extracted ASI Loader is not x64 (machine=0x{0:X4}); refusing to vendor it." -f $machine)
    }

    $dllSha = (Get-FileHash -LiteralPath $tempDll -Algorithm SHA256).Hash.ToLower()

    # Idempotency: an upstream that has not moved must leave the tree clean.
    # Without this the fetched-at line rewrites README.md on every run, so
    # `git status` after a no-op refresh shows a timestamp-only diff with no
    # artifact behind it.
    # The TAG is part of this, not just the hash. Upstream can publish a new tag
    # carrying a byte-identical x64 DLL, and on the hash alone the run would
    # return here having refreshed nothing - leaving the vendored README and
    # install.cmd's ASI_LOADER_VERSION naming the older release for good.
    $vendoredTag = if (Test-Path -LiteralPath $vendorReadme) {
        $m = Select-String -LiteralPath $vendorReadme -List -Pattern '^- Tag: `(.+)`'
        if ($m) { $m.Matches[0].Groups[1].Value } else { $null }
    } else { $null }

    if ((Test-Path -LiteralPath $vendorDll) -and (Test-Path -LiteralPath $vendorLicense) -and (Test-Path -LiteralPath $vendorReadme) -and
        ($vendoredTag -eq $meta.Tag) -and
        ((Get-FileHash -LiteralPath $vendorDll -Algorithm SHA256).Hash.ToLower() -eq $dllSha)) {
        Write-Host "  no change (tag=$($meta.Tag) sha256=$($dllSha.Substring(0,12))... matches on-disk vendor copy)" -ForegroundColor DarkGray
        Write-Host ""
        Write-Host "vendor/ultimate-asi-loader is already up to date." -ForegroundColor Green
        return
    }

    Move-Item -LiteralPath $tempDll -Destination $vendorDll -Force

    if (Test-Path -LiteralPath $tempLicense) {
        Move-Item -LiteralPath $tempLicense -Destination $vendorLicense -Force
    } else {
        $licenseUrl = "https://raw.githubusercontent.com/ThirteenAG/Ultimate-ASI-Loader/$($meta.Tag)/license"
        Invoke-WebRequest -Uri $licenseUrl -OutFile $vendorLicense -UseBasicParsing -TimeoutSec 30 -Headers @{ "User-Agent" = "CameraUnlock-HeadTracking" }
    }

    $readme = @(
        '# Ultimate ASI Loader (vendored)',
        '',
        'Bundled copy of Ultimate ASI Loader, the install-time source of truth.',
        'install.cmd extracts directly from here and never reaches out to the network.',
        'Refresh manually with `pixi run update-deps`, then commit.',
        '',
        '## Snapshot',
        '',
        '- Upstream: https://github.com/ThirteenAG/Ultimate-ASI-Loader',
        "- Tag: ``$($meta.Tag)``",
        "- Commit: ``$($meta.CommitSha)``",
        "- Asset: ``$($meta.AssetName)``",
        "- dinput8.dll SHA-256: ``$dllSha``",
        "- Fetched at: $($meta.FetchedAt)",
        '',
        '`dinput8.dll` is extracted from the upstream asset untouched; the zip itself is a',
        'download intermediate and is not kept. The loader picks the system library it',
        "forwards to from its own filename at run time, so install.cmd copies this one",
        "binary into the game directory as ``$proxyName`` - the import TheGreatCircle.exe",
        'already has, and one Windows resolves from the game directory rather than from',
        'System32.'
    ) -join "`n"
    # Set-TextFileNoBom, not `Set-Content -Encoding UTF8`: in Windows PowerShell
    # 5.1 that switch means UTF-8 WITH a byte order mark, and this file ships
    # inside the installer ZIP. -Path is also the wildcard-expanded parameter
    # that Copy-FileLiteral exists to avoid.
    Set-TextFileNoBom -Path $vendorReadme -Text ($readme + "`n")

    # The loader version recorded in the state file that the standalone
    # install.cmd route writes. (The launcher never sees it: this package is
    # delivery_mode "manifest" and the launcher does not run install.cmd.)
    # Stamped here rather than kept by hand - its own comment says to bump it
    # alongside vendor/ through this script, which then never wrote to it, so
    # the next successful refresh shipped a state file naming the previous
    # loader.
    #
    # Only when it differs. Update-VersionInFile throws when the substitution
    # changes nothing, which is right for a renamed key and wrong here: upstream
    # can rebuild an asset under an existing tag, and the hash guard above lets
    # that through, so an unconditional stamp would abort with a misleading
    # message after vendor/ had already been rewritten.
    $loaderVersion = $meta.Tag -replace '^v', ''
    $installText = [System.IO.File]::ReadAllText($project.InstallCmdPath)
    if ($installText -notmatch '(?m)^set "ASI_LOADER_VERSION=[0-9.]*"') {
        throw "install.cmd has no ASI_LOADER_VERSION line to stamp; the CONFIG BLOCK has changed shape."
    }
    # Anchored to the start of a line, like the presence check above. Unanchored,
    # a commented example of the same setting anywhere in the file would satisfy
    # it and the real `set` line would silently keep the previous version.
    if ($installText -notmatch ('(?m)^' + [regex]::Escape("set `"ASI_LOADER_VERSION=$loaderVersion`""))) {
        Update-VersionInFile -Path $project.InstallCmdPath `
            -Pattern '(?m)^set "ASI_LOADER_VERSION=[0-9.]*"' `
            -Replacement "set `"ASI_LOADER_VERSION=$loaderVersion`""
    }

    Write-Host "  tag=$($meta.Tag) sha256=$($dllSha.Substring(0,12))..." -ForegroundColor DarkGray
} finally {
    Remove-Item $tempDir -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ""
Write-Host "vendor/ultimate-asi-loader refreshed. Review and commit." -ForegroundColor Green
