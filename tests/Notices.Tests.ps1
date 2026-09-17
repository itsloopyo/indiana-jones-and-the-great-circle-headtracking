# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

<#
.SYNOPSIS
    THIRD-PARTY-NOTICES.md states facts about this repo. These check they are
    still true.

.DESCRIPTION
    The notices file is the auditable boundary: it is what says where each piece
    of third-party code came from and at which commit. Every version string in
    it is a claim about something else in the tree, and nothing was checking any
    of them.

    The cameraunlock-core pin is the one that actually moves. `pixi run sync`
    advances the submodule whenever core gains a commit, and the notices entry
    beside it does not follow - it was found stating a commit two days and
    several changes behind the one the mod was built against, having passed
    `validate-notices` the whole time, because that gate checks that every
    component is ACCOUNTED FOR and not that the accounting is accurate.

    A wrong commit in a licence notice is a small thing right up until somebody
    audits it, at which point it is the one document that was supposed to be
    right.
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Import-Module (Join-Path $PSScriptRoot 'TestSupport.psm1') -Force
Import-Module (Join-Path $PSScriptRoot '..\scripts\ModProject.psm1') -Force

$project = Get-ModProject
$noticesPath = Join-Path $project.Root 'THIRD-PARTY-NOTICES.md'
$notices = Get-Content -LiteralPath $noticesPath -Raw

# ---- cameraunlock-core -------------------------------------------------------

# `git submodule status` prefixes the sha with -, + or U when the working copy
# is not exactly at the recorded commit. The recorded commit is what ships, so
# the marker is stripped rather than treated as part of the sha.
Push-Location $project.Root
try {
    $statusLine = (& git submodule status cameraunlock-core) -join ''
} finally { Pop-Location }

$pinned = ''
if ($statusLine -match '([0-9a-f]{40})') { $pinned = $Matches[1] }

Check 'git reports a commit for the cameraunlock-core submodule' `
    ($pinned -ne '') "git submodule status said '$statusLine'"

$claimed = ''
if ($notices -match '(?s)##\s+cameraunlock-core.*?submodule at `([0-9a-f]{40})`') {
    $claimed = $Matches[1]
}

Check 'THIRD-PARTY-NOTICES.md states a cameraunlock-core commit' `
    ($claimed -ne '') 'no "submodule at <sha>" line under the cameraunlock-core heading'

Check 'the cameraunlock-core commit in the notices is the one the submodule is pinned to' `
    ($claimed -eq $pinned) "notices say '$claimed', submodule is pinned at '$pinned' - run pixi run sync and update the notices together"

# ---- Ultimate ASI Loader -----------------------------------------------------

# The vendor README is the authority for what is actually on disk: it records
# the asset, tag, commit and hash of the binary that was fetched. The notices
# entry is a copy of that, so the two have to agree or one of them is lying
# about what ships.
$vendorReadme = Get-Content -LiteralPath (Join-Path $project.VendorLoaderDir 'README.md') -Raw

$vendorCommit = ''
if ($vendorReadme -match 'Commit:\s*`([0-9a-f]{40})`') { $vendorCommit = $Matches[1] }

$noticesLoaderCommit = ''
if ($notices -match '(?s)##\s+Ultimate ASI Loader.*?commit `([0-9a-f]{40})`') {
    $noticesLoaderCommit = $Matches[1]
}

Check 'the vendored loader README records a commit' `
    ($vendorCommit -ne '') 'no "Commit:" line in vendor/ultimate-asi-loader/README.md'

Check 'the loader commit in the notices matches the vendored README' `
    ($noticesLoaderCommit -eq $vendorCommit) "notices say '$noticesLoaderCommit', vendor README says '$vendorCommit'"

# ---- the vendored binary actually exists -------------------------------------

# ModProject names the file the deploy and the packager both copy. It is the
# upstream asset's own name, and install.cmd renames it to the proxy the game
# imports - so this is checking the SOURCE exists, not the deployed name.
Check 'the vendored loader file ModProject points at is present' `
    (Test-Path -LiteralPath $project.VendorLoaderDll) "missing: $($project.VendorLoaderDll)"

Check 'the notices name the same vendored loader file that is on disk' `
    ($notices -match [regex]::Escape((Split-Path -Leaf $project.VendorLoaderDll))) `
    "THIRD-PARTY-NOTICES.md never mentions $(Split-Path -Leaf $project.VendorLoaderDll)"

exit (Complete-Checks)
