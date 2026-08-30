#!/usr/bin/env pwsh
#Requires -Version 5.1
# Dev uninstall: strips the mod from EVERY Fallout 4 install on this machine.
#
# uninstall.cmd resolves a single path (see find-installs.ps1), so on its own it
# leaves the other copy of the game modded. /force is unconditional here because
# a dev deploy writes no state file, and without it the dxgi.dll proxy stays.

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

$installs = @(& (Join-Path $scriptDir "find-installs.ps1"))

Write-Host "Fallout 4 installs found: $($installs.Count)" -ForegroundColor Cyan
$uninstall = Join-Path $scriptDir "uninstall.cmd"
foreach ($install in $installs) {
    Write-Host ""
    Write-Host "--- $install" -ForegroundColor Cyan
    & $uninstall $install /y /force
    if ($LASTEXITCODE -ne 0) {
        throw "uninstall.cmd failed for '$install' (exit code $LASTEXITCODE)."
    }
}
