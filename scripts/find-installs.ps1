#!/usr/bin/env pwsh
#Requires -Version 5.1
# Emits every Fallout 4 install on this machine, detection order first.
#
# Find-GamePath stops at its first hit (Steam app id beats the GOG registry) and
# Fallout 4 is routinely owned on both stores, so a dev install or uninstall
# driven by it acts on one copy while the game being launched is the other.

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptDir

Import-Module (Join-Path $projectRoot "cameraunlock-core\powershell\GamePathDetection.psm1") -Force

$config = (Get-GameConfigs)['fallout-4']
$executable = $config.Executable

$candidates = @()
$envPath = [Environment]::GetEnvironmentVariable($config.EnvVar)
if ($envPath) { $candidates += $envPath }
$candidates += Find-SteamGameByAppId -AppId $config.SteamAppId -Executable $executable
foreach ($library in Find-SteamLibraries) {
    $candidates += (Join-Path $library "steamapps\common\$($config.SteamFolder)")
}
$candidates += Find-GogGamePath -GogGameIds $config.GogGameIds -Executable $executable

$installs = [System.Collections.Generic.List[string]]::new()
foreach ($candidate in $candidates) {
    if (-not $candidate) { continue }
    if (-not (Test-GameInstallation -Path $candidate -Executable $executable)) { continue }
    $full = [System.IO.Path]::GetFullPath($candidate).TrimEnd('\')
    if ($installs -contains $full) { continue }
    $installs.Add($full)
}

if ($installs.Count -eq 0) {
    throw "No Fallout 4 install found (looked at $($config.EnvVar), Steam and GOG)."
}

$installs
