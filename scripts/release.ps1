# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

<#
.SYNOPSIS
    Cuts a release: stamps the version, regenerates the changelog, builds and
    packages, then commits, tags and pushes.

.DESCRIPTION
    Runs unattended. `pixi run release minor` is the authorization; there is no
    second gate, and nothing here reads stdin. The preconditions (on main, clean
    tree, tag absent, resolvable version) are the safety net, and each one exits
    non-zero with a single line of diagnostic.

    The file mechanics live in ReleaseSupport.psm1; what is left here is the
    policy - the order of operations and what gets committed.

.PARAMETER Version
    major, minor, patch, or a literal X.Y.Z. `nightly` instead hands off to
    release-nightly.ps1, which publishes the rolling `dev` pre-release and
    cuts no version.

.PARAMETER Force
    Release even when every commit since the last tag is noise (writes a
    maintenance changelog entry instead of aborting).
#>
[CmdletBinding()]
param(
    # NOT Mandatory: PowerShell satisfies a missing mandatory parameter by
    # reading stdin, and `pixi run release` allocates no TTY - that prompt
    # dies with "IOException: The handle is invalid" instead of printing a
    # usage line. Validate it ourselves and fail fast.
    [Parameter(Position = 0)][string]$Version,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

if (-not $Version) {
    Write-Host "Usage: pixi run release <major|minor|patch|nightly|X.Y.Z> [-Force]" -ForegroundColor Red
    exit 1
}

if ($Version -eq 'nightly') {
    & (Join-Path $PSScriptRoot 'release-nightly.ps1')
    exit $LASTEXITCODE
}

Import-Module (Join-Path $PSScriptRoot 'ModProject.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'ReleaseSupport.psm1') -Force
$project = Get-ModProject
Import-Module (Join-Path $project.Root 'cameraunlock-core/powershell/ReleaseWorkflow.psm1') -Force

Push-Location $project.Root
try {
    $current = Get-ModVersion
    $new = Resolve-ReleaseVersion -Argument $Version -CurrentVersion $current
    if (-not (Test-SemanticVersion -Version $new)) { throw "Resolved version '$new' is not X.Y.Z." }

    # New-ReleaseTag pushes to `main`, so releasing from any other branch would
    # push commits the branch does not contain. Gate before anything mutates.
    # Subexpression-stringified, not .Trim() on the raw result: `git rev-parse`
    # outside a checkout prints to stderr and returns nothing, and $null.Trim()
    # then buries the real cause under a method-call error.
    $branch = "$(git rev-parse --abbrev-ref HEAD)".Trim()
    if (-not $branch) { throw 'Could not read the current branch. Is this a git checkout?' }
    if ($branch -ne 'main') { throw "Releases cut from 'main' only; currently on '$branch'." }
    if (-not (Test-CleanGitStatus)) { throw "Working tree is dirty - commit or stash first." }
    if (Test-GitTagExists -Tag "v$new") { throw "Tag v$new already exists." }


    # Generate CHANGELOG from commits since the last tag. This is the gate that
    # aborts when there are no user-facing commits, so run it BEFORE stamping
    # any version or building - a failure here then leaves a clean tree instead
    # of stranding a half-applied bump with no tag.
    #
    # The SAME path filter .github/workflows/release.yml hands to
    # generate-release-notes.ps1. Without it the shipped CHANGELOG was drawn from
    # every commit since the last tag while the GitHub release body was drawn
    # from these paths only, so one version had two different accounts of itself
    # - and this repo's history is mostly commits to the paths that differ
    # (CMakeLists.txt, pixi.toml, tests/, scripts/*.ps1). Keep the two lists
    # identical: these are the paths whose commits change something a player
    # receives.
    $shippingPaths = @(
        'src/'
        'cameraunlock-core/'
        'scripts/install.cmd'
        'scripts/uninstall.cmd'
        'vendor/'
        'launcher-manifest.json'
    )
    # An "## [Unreleased]" section with anything under it has to be dealt with
    # before the release, not after. New-ChangelogFromCommits inserts the new
    # "## [X.Y.Z]" entry directly under "# Changelog", which is ABOVE that
    # heading, and nothing in the release path ever clears it - so the section
    # survives, and package-release.ps1 stages CHANGELOG.md into the installer
    # ZIP. Every player extracting the release would get a permanent "Unreleased"
    # list of the features that had just shipped, sitting under the entry saying
    # they shipped.
    #
    # Refused rather than silently dropped: those bullets are hand-written and
    # usually say it better than the generated commit subjects do, so the right
    # move is to fold them in, which only the person writing the release can do.
    $changelogPath = Join-Path $project.Root 'CHANGELOG.md'
    $unreleased = [regex]::Match(
        [System.IO.File]::ReadAllText($changelogPath),
        '(?ms)^##\s*\[Unreleased\][^
]*?
(.*?)(?=^##\s|\z)')
    if ($unreleased.Success -and $unreleased.Groups[1].Value.Trim()) {
        Write-Host "Error: CHANGELOG.md still has an [Unreleased] section with content." -ForegroundColor Red
        Write-Host "The release entry is inserted above it and nothing clears it, so that text would ship" -ForegroundColor Yellow
        Write-Host "inside the installer ZIP describing the release as unreleased. Fold those bullets into" -ForegroundColor Yellow
        Write-Host "the release (or delete them), then re-run." -ForegroundColor Yellow
        exit 1
    }

    try {
        New-ChangelogFromCommits -ChangelogPath 'CHANGELOG.md' -Version $new `
            -ArtifactPaths $shippingPaths | Out-Null
    } catch {
        if (-not $Force) {
            Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
            Write-Host "No user-facing changes to release. Re-run with -Force for a maintenance release." -ForegroundColor Yellow
            exit 1
        }
        # Printed even under -Force: this catch sees every failure of
        # New-ChangelogFromCommits, not only "all commits filtered as noise". A
        # missing CHANGELOG.md or a git failure would otherwise be relabelled
        # "no user-facing changes" and released.
        Write-Host "Changelog generation failed: $($_.Exception.Message)" -ForegroundColor Yellow
        Write-Host "Writing maintenance entry (-Force)." -ForegroundColor Yellow
        Add-MaintenanceChangelogEntry -Path (Join-Path $project.Root 'CHANGELOG.md') -NewVersion $new
    }

    # From here to the manifest check below, every step writes to the tree, and
    # the last two of them can still fail. The catch puts the tree back rather
    # than stranding a stamped, unreleased, uncommitted working copy whose only
    # symptom next time is this script's own clean-tree precondition failing on
    # files the previous run wrote.
    #
    # CMakeLists.txt is canonical; the other two are hand-kept copies that drift
    # silently when skipped (install.cmd prints MOD_VERSION to the user). One
    # table, so the set that gets stamped and the set that gets committed cannot
    # disagree.
    $projectName = [regex]::Escape($project.Name)
    $stamps = @(
        @{
            Path        = 'CMakeLists.txt'
            Pattern     = "project\($projectName VERSION [0-9.]+"
            Replacement = "project($($project.Name) VERSION $new"
        }
        @{
            Path        = 'pixi.toml'
            Pattern     = '(?m)^version = "[0-9.]+"'
            Replacement = "version = `"$new`""
        }
        @{
            Path        = 'scripts/install.cmd'
            Pattern     = '(?m)^set "MOD_VERSION=[0-9.]+"'
            Replacement = "set `"MOD_VERSION=$new`""
        }
        @{
            # The launcher reads this to decide whether an update is available,
            # so a stale version here pins every user on the release that
            # shipped it.
            Path        = 'launcher-manifest.json'
            Pattern     = '(?m)^(\s*)"version": "[0-9.]+"'
            Replacement = "`$1`"version`": `"$new`""
        }
    )
    $writtenFiles = @($stamps | ForEach-Object { $_.Path }) + 'CHANGELOG.md' + 'THIRD-PARTY-NOTICES.md'
    try {
        # Re-point THIRD-PARTY-NOTICES.md at the pinned cameraunlock-core commit.
        # Copy-SharedBundle asserts the two agree and throws when they do not, and
        # it runs inside `pixi run package` below - so without this a release
        # taken after a submodule bump would stamp every version file, fail in the
        # packager, and strand a half-applied bump with no tag.
        #
        # INSIDE the rollback, not above it. The changelog was already rewritten
        # by the gate a few lines up, and $writtenFiles covers it, so a throw
        # from here used to escape past the only catch that puts it back: the
        # release aborted with a modified CHANGELOG.md carrying an unreleased
        # heading, and the next attempt failed its own clean-tree precondition
        # pointing at a file the previous run wrote. That is the exact failure
        # the ordering note above the changelog gate exists to prevent, and this
        # step was the one thing still outside the guard.
        #
        # Run as a child process so its exit code is definite. A .ps1 invoked
        # with & that returns normally does not set $LASTEXITCODE at all, so this
        # test used to read back whatever the last native command in the script
        # happened to leave there.
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $project.Root 'cameraunlock-core/scripts/sync-core-notices.ps1') -Repo $project.Root
        if ($LASTEXITCODE -ne 0) {
            throw "sync-core-notices.ps1 could not point THIRD-PARTY-NOTICES.md at the pinned cameraunlock-core commit (exit $LASTEXITCODE). The packager asserts the two agree, so fix the notices file before releasing."
        }

        foreach ($stamp in $stamps) {
            Update-VersionInFile -Path (Join-Path $project.Root $stamp.Path) -Pattern $stamp.Pattern -Replacement $stamp.Replacement
        }

        # The seven suites, HERE and not only in build.yml. build.yml skips any
        # commit whose message starts "Release v", which is exactly what this
        # script commits, and the reusable release workflow runs `pixi run
        # package` and nothing else - so without this the one commit that ships
        # is the one commit nothing tests. Before the package, so a failure costs
        # nothing to undo.
        & pixi run test
        if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }

        # Build and package through the same pixi chain CI runs (setup -> build
        # -> package), not a bare `cmake --build`, which fails outright on a
        # checkout where build/ was never configured.
        & pixi run package
        if ($LASTEXITCODE -ne 0) { throw 'Build/packaging failed' }

        # The launcher's contract, checked against the ZIP that is about to be
        # published rather than against the source tree: every files[].source has
        # to exist inside the archive. A mismatch here is invisible until
        # someone's launcher install fails, which is why the skill asks for it in
        # the release flow. The ZIP is already built, so this only runs the
        # validator.
        & node (Join-Path $project.Root 'cameraunlock-core/scripts/validate-manifest.mjs')
        if ($LASTEXITCODE -ne 0) { throw 'launcher-manifest.json does not validate against the packaged ZIP' }
    } catch {
        git checkout -- $writtenFiles
        throw
    }

    # The same set the rollback above covers: THIRD-PARTY-NOTICES.md is in it
    # because the sync can have rewritten it, and leaving that out commits a
    # release whose shipped attribution names a different core commit from the
    # one it was built against.
    $versionFiles = $writtenFiles
    git add -- $versionFiles
    if ($LASTEXITCODE -ne 0) { throw "git add failed for: $($versionFiles -join ', ')" }
    # Subject matched by build.yml, which skips the duplicate build of the
    # commit release.yml is about to build from the tag.
    git commit -m "Release v$new"
    if ($LASTEXITCODE -ne 0) { throw 'Failed to commit the version bump' }

    New-ReleaseTag -Version $new -Message "Release v$new"
    Write-Host "Released v$new" -ForegroundColor Green
} finally {
    Pop-Location
}
