# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

<#
.SYNOPSIS
    Copies the freshly built .asi and the vendored ASI loader into every local
    Indiana Jones and the Great Circle install.

.DESCRIPTION
    A wrapper around Invoke-DevDeployASILoader, which owns the whole deploy:
    resolving every install through Find-AllGamePaths, refusing to write while
    the game is running (a loaded .asi holds its own file open and the copy
    fails halfway), and deriving the exe directory per store rather than
    assuming the Steam layout - the Game Pass copy can name and site its exe
    differently, and this machine has only that copy.

    Resolving any of that here instead would be a second copy of the rule, and
    the copies drift in the direction nobody notices: the dev loop writes
    somewhere the shipped installer does not.

.PARAMETER GamePath
    Install root to deploy into. Omitted, every copy of the game on this
    machine is located and written to - the title ships on Steam and on Xbox
    Game Pass, owning it twice is ordinary, and a deploy that picks one
    silently leaves the other running whatever build was last dropped in it.
#>
[CmdletBinding()]
param([Parameter(Position = 0)][string]$GamePath)

$ErrorActionPreference = 'Stop'

Import-Module (Join-Path $PSScriptRoot 'ModProject.psm1') -Force
$project = Get-ModProject
# No -Force: it is Remove-Module + re-import, and Remove-Module unloads from the
# caller's session too, taking ModProject's exports with it.
Import-Module (Join-Path $project.Root 'cameraunlock-core/powershell/DevDeploy.psm1')

try {
    # install.cmd owns which import the loader is renamed onto; reading it back
    # is what keeps the dev loop testing the same proxy the installer deploys.
    # A proxy the game does not import is never loaded, and the mod then writes
    # no log at all to explain itself.
    $loaderName = Get-AsiLoaderProxyName -InstallCmdPath $project.InstallCmdPath

    # -ExtraDlls @() explicitly: the orchestrator's default dependency list is
    # the Unity core assemblies, and this mod ships a single self-contained .asi.
    Invoke-DevDeployASILoader `
        -GameId $project.Id `
        -GameDisplayName $project.DisplayName `
        -BuildOutputPath (Split-Path -Parent $project.BuildOutputPath) `
        -ModDllName $project.AsiFileName `
        -VendorLoaderDll $project.VendorLoaderDll `
        -AsiLoaderName $loaderName `
        -ExtraDlls @() `
        -GivenPath $GamePath | Out-Null
} catch {
    # One red line and exit 1, so a `pixi run install` that cannot find
    # something reads the same whichever step gave up.
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
