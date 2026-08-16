# Print the PE fingerprint of a Fallout 4 EXE as a paste-ready build profile.
#
# One address in this mod is pinned per build - the VATS flag in .data, which has
# no RTTI, no vtable and no code signature to find it by at runtime. Every other
# address is discovered while the game runs, so a patch moves it for free.
#
# Run this first when a user reports "the VATS gate does not know this build" in
# HeadTracking.log. If the fingerprint below already appears in
# src/game/game_state.cpp then the patch left the EXE alone (asset-only patches
# move the Steam buildid without relinking) and there is nothing to re-derive.
[CmdletBinding()]
param([string]$ExePath)

$ErrorActionPreference = 'Stop'

if (-not $ExePath) {
    Import-Module (Join-Path $PSScriptRoot '..\cameraunlock-core\powershell\GamePathDetection.psm1') -Force
    $gamePath = Find-GamePath -GameId 'fallout-4'
    if (-not $gamePath) { throw "Fallout 4 not found. Pass -ExePath explicitly." }
    $ExePath = Join-Path $gamePath 'Fallout4.exe'
}
if (-not (Test-Path $ExePath)) { throw "No such file: $ExePath" }

$bytes = [System.IO.File]::ReadAllBytes($ExePath)
$peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
if ([BitConverter]::ToUInt32($bytes, $peOffset) -ne 0x00004550) {
    throw "$ExePath is not a PE image (no PE signature at 0x$($peOffset.ToString('X')))"
}

# COFF header follows the 4-byte signature; the optional header follows that.
$coff = $peOffset + 4
$stamp = [BitConverter]::ToUInt32($bytes, $coff + 4)
$optional = $coff + 20
$sizeOfImage = [BitConverter]::ToUInt32($bytes, $optional + 56)
$checkSum = [BitConverter]::ToUInt32($bytes, $optional + 64)

$built = ([DateTimeOffset]::FromUnixTimeSeconds($stamp)).UtcDateTime
$name = 'steam-win64-' + $built.ToString('yyyyMMdd')

Write-Host ""
Write-Host "$ExePath"
Write-Host ("  linked {0:yyyy-MM-dd HH:mm:ss} UTC" -f $built)
Write-Host ""
Write-Host "Add to kKnownProfiles in src/game/game_state.cpp, at the TOP of the array."
Write-Host "The VATS offset stays 0 until it is re-derived with Ctrl+Shift+V (in gameplay)"
Write-Host "and Ctrl+Shift+X (in VATS); a zero offset keeps the gate off, which is the same"
Write-Host "behaviour as an unknown build and is safe to ship."
Write-Host ""
Write-Host ("    {{ ""{0}"", 0x{1:X8}, 0x{2:X8}, 0x{3:X8}, 0x0 }}," -f $name, $stamp, $sizeOfImage, $checkSum)
Write-Host ""
