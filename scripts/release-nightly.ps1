#!/usr/bin/env pwsh
#Requires -Version 5.1

[CmdletBinding()]
param([switch]$AllowDirty)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')

Import-Module (Join-Path $ProjectRoot 'cameraunlock-core\powershell\NightlyRelease.psm1') -Force

# manifest.json is the canonical version source; package-release.ps1 names the
# installer ZIP from it, and Publish-NightlyBuild derives that path.
$version = (Get-Content (Join-Path $ProjectRoot 'manifest.json') -Raw | ConvertFrom-Json).version

Publish-NightlyBuild `
    -ModId 'fallout-4' `
    -ModName 'Fallout4HeadTracking' `
    -Version $version `
    -ProjectRoot $ProjectRoot `
    -AllowDirty:$AllowDirty
