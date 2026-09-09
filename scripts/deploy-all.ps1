#!/usr/bin/env pwsh
#Requires -Version 5.1
# Dev deploy to EVERY Fallout 4 install on this machine (see find-installs.ps1) -
# Steam, GOG and Game Pass copies alike.
# Pass a path to scripts/deploy.ps1 directly to target a single one.

param(
    [Parameter(Mandatory=$true, Position=0)]
    [ValidateSet("Debug", "Release")]
    [string]$Configuration
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

$installs = @(& (Join-Path $scriptDir "find-installs.ps1"))

Write-Host "Fallout 4 installs found: $($installs.Count)" -ForegroundColor Cyan
foreach ($install in $installs) {
    Write-Host ""
    Write-Host "--- $install" -ForegroundColor Cyan
    & (Join-Path $scriptDir "deploy.ps1") $Configuration $install
}
