#!/usr/bin/env pwsh
#Requires -Version 5.1
# Emits every Fallout 4 install on this machine, detection order first.
#
# Find-GamePath stops at its first hit (Steam app id beats the GOG registry,
# which beats Game Pass) and Fallout 4 is routinely owned on more than one
# store, so a dev install or uninstall driven by it acts on one copy while the
# game being launched is another.

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptDir

Import-Module (Join-Path $projectRoot "cameraunlock-core\powershell\GamePathDetection.psm1") -Force

$installs = @(Find-AllGamePaths -GameId 'fallout-4')

if ($installs.Count -eq 0) {
    throw "No Fallout 4 install found (looked at FALLOUT_4_PATH, Steam, GOG and the Xbox app's install roots)."
}

$installs
