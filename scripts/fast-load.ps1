#!/usr/bin/env pwsh
#Requires -Version 5.1
# Speed up Fallout 4 launch for debug iteration:
#   1. Rename startup videos (Data\Video\*.bk2) so the engine skips them.
#   2. Write Fallout4Custom.ini in "Documents\My Games\Fallout 4" with
#      sIntroSequence= (empty) and uMainMenuDelayBeforeAllowSkip=0 so the
#      intro chain is short-circuited at the engine level too.
#
# Reversible:  scripts\fast-load.ps1 -Restore
#
# Game path resolution order:
#   -GamePath arg  ->  every install find-installs.ps1 reports.
# The videos live inside each install, so a machine with the game from two
# stores needs both done: fast-loading one and launching the other looks
# exactly like the intro skip not working. The ini is per user, so it is
# written once whatever the install count.

param(
    [string]$GamePath,
    [switch]$Restore
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot

function Resolve-GamePaths {
    param([string]$Given)
    if ($Given) {
        if (-not (Test-Path $Given)) { throw "GamePath does not exist: $Given" }
        return @((Resolve-Path $Given).Path)
    }
    return @(& (Join-Path $PSScriptRoot 'find-installs.ps1'))
}

$gameRoots = Resolve-GamePaths -Given $GamePath

# Files that play before the game is interactive. MainMenuLoop is the
# looping background behind the main menu - skipping it leaves the menu on
# a black background, which is what we want for fast iteration.
$targets = @(
    'GameIntro_V3_B.bk2',   # "Bethesda Game Studios" pre-menu intro
    'Intro.bk2',            # Sole Survivor pre-war intro on New Game
    'MainMenuLoop.bk2'      # Looping main menu background
)

$docsIni = Join-Path $env:USERPROFILE 'Documents\My Games\Fallout 4\Fallout4Custom.ini'

Write-Host "Fallout 4 installs found: $($gameRoots.Count)" -ForegroundColor Cyan

foreach ($gameRoot in $gameRoots) {
    $videoDir = Join-Path $gameRoot 'Data\Video'
    if (-not (Test-Path $videoDir)) { throw "Video folder not found: $videoDir" }

    if ($Restore) {
        Write-Host "Restoring startup videos in $videoDir" -ForegroundColor Cyan
        foreach ($name in $targets) {
            $disabled = Join-Path $videoDir ($name + '.disabled')
            $original = Join-Path $videoDir $name
            if (Test-Path $disabled) {
                if (Test-Path $original) { throw "Both $name and $name.disabled exist - resolve manually." }
                Move-Item $disabled $original
                Write-Host "  restored $name"
            } else {
                Write-Host "  (skip)   $name not disabled" -ForegroundColor DarkGray
            }
        }
        continue
    }

    Write-Host "Disabling startup videos in $videoDir" -ForegroundColor Cyan
    foreach ($name in $targets) {
        $original = Join-Path $videoDir $name
        $disabled = Join-Path $videoDir ($name + '.disabled')
        if (Test-Path $disabled) {
            Write-Host "  (already)  $name" -ForegroundColor DarkGray
            continue
        }
        if (-not (Test-Path $original)) {
            Write-Host "  (missing)  $name" -ForegroundColor Yellow
            continue
        }
        Move-Item $original $disabled
        Write-Host "  disabled   $name"
    }
}

if ($Restore) {
    if (Test-Path $docsIni) {
        $backup = $docsIni + '.fastload-backup'
        if (Test-Path $backup) {
            Move-Item $backup $docsIni -Force
            Write-Host "  restored Fallout4Custom.ini from backup" -ForegroundColor Cyan
        } else {
            Remove-Item $docsIni
            Write-Host "  removed Fallout4Custom.ini (no prior backup)" -ForegroundColor Cyan
        }
    }
    Write-Host "Done." -ForegroundColor Green
    return
}

$iniDir = Split-Path -Parent $docsIni
if (-not (Test-Path $iniDir)) { New-Item -ItemType Directory -Path $iniDir -Force | Out-Null }

if ((Test-Path $docsIni) -and -not (Test-Path ($docsIni + '.fastload-backup'))) {
    Copy-Item $docsIni ($docsIni + '.fastload-backup')
}

$iniBody = @(
    '[General]',
    'sIntroSequence=',
    'uMainMenuDelayBeforeAllowSkip=0',
    ''
) -join "`r`n"
Set-Content -Path $docsIni -Value $iniBody -Encoding ASCII
Write-Host "Wrote $docsIni" -ForegroundColor Cyan

Write-Host ""
Write-Host "Fast-load applied. Launch the game; the main menu should appear immediately." -ForegroundColor Green
Write-Host "Revert with: scripts\fast-load.ps1 -Restore" -ForegroundColor DarkGray
