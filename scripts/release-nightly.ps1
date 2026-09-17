# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

# Thin shim. Determine version, delegate to the shared publisher.
# See cameraunlock-core/powershell/NightlyRelease.psm1 for what it does.

[CmdletBinding()]
param(
    [switch]$AllowDirty
)

$ErrorActionPreference = 'Stop'

Import-Module (Join-Path $PSScriptRoot 'ModProject.psm1') -Force
$project = Get-ModProject
Import-Module (Join-Path $project.Root 'cameraunlock-core/powershell/NightlyRelease.psm1') -Force

# CMakeLists.txt is this mod's canonical version, and the string
# package-release.ps1 puts in the installer ZIP filename the publisher looks
# for. There is no constants.h version macro here.
$version = Get-ModVersion

# SPLATTED, not backtick-continued, and that is the whole point of the shape.
#
# A backtick continuation cannot span a comment: the comment line has no
# trailing backtick, so the statement ENDS there and every argument below it is
# silently dropped. This call was written that way with a comment in the middle
# of the parameter list, and the result parsed without a single error while
# passing only the first four arguments. -BuildCommand then fell back to the
# module's default of `pixi run build-release`, a task this repo does not have,
# and the nightly died on a message about a task nobody had ever written.
# -NoNexusZip and -AllowDirty went the same way and would have been the next two
# failures.
#
# A hashtable takes comments between its entries safely, which is what lets each
# choice below be explained where it is made.
$publish = @{
    ModId       = $project.Id
    ModName     = $project.Name
    Version     = $version
    ProjectRoot = $project.Root

    # `test`, not `build`. The task depends on `build`, so this compiles and then
    # evaluates the assertions, where `pixi run build` only compiled them. A
    # nightly is a binary handed to someone to play, and it had the same hole the
    # release path did: nothing ran a suite before it was published.
    BuildCommand = 'pixi run test'

    # This mod is installer-only: the payload sits beside the game exe and no mod
    # manager can deploy it there. The publisher's default treats a missing Nexus
    # ZIP as fatal, so without this every nightly fails. See the notes at the top
    # of package-release.ps1 for why the stage is gone.
    NoNexusZip = $true

    AllowDirty = [bool]$AllowDirty
}

Publish-NightlyBuild @publish
