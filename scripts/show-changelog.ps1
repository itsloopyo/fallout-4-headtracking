#!/usr/bin/env pwsh
#Requires -Version 5.1

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

Import-Module (Join-Path $projectDir "cameraunlock-core\powershell\ReleaseWorkflow.psm1") -Force

$artifactPaths = @(
    "src/",
    "cameraunlock-core/",
    "scripts/install.cmd",
    "scripts/uninstall.cmd"
)

$lastTag = git describe --tags --abbrev=0 2>$null
if ($LASTEXITCODE -ne 0) {
    $commitRange = $null
    Write-Host "(no previous tags found - showing all commits)" -ForegroundColor Gray
    $commits = git log --pretty=format:"%s" --reverse --no-merges -- $artifactPaths
} else {
    $commitRange = "$lastTag..HEAD"
    Write-Host "(changes since $lastTag)" -ForegroundColor Gray
    $commits = git log $commitRange --pretty=format:"%s" --reverse --no-merges -- $artifactPaths
}

if (-not $commits) {
    Write-Host "No commits found." -ForegroundColor Yellow
    exit 0
}

$commits = @($commits | Where-Object { -not (Test-NoiseCommit $_) })

if ($commits.Count -eq 0) {
    Write-Host "All commits were filtered as noise." -ForegroundColor Yellow
    exit 0
}

$features = @()
$fixes = @()
$changes = @()
$other = @()

foreach ($commit in $commits) {
    if ($commit -match '^feat(\(.*?\))?:\s*(.+)$') {
        $features += "- $($matches[2])"
    } elseif ($commit -match '^fix(\(.*?\))?:\s*(.+)$') {
        $fixes += "- $($matches[2])"
    } elseif ($commit -match '^perf(\(.*?\))?:\s*(.+)$') {
        $changes += "- $($matches[2])"
    } else {
        $other += "- $commit"
    }
}

$date = Get-Date -Format 'yyyy-MM-dd'
Write-Host ""
Write-Host "## [NEXT] - $date" -ForegroundColor Cyan
Write-Host ""

if ($features.Count -gt 0) {
    Write-Host "### Added" -ForegroundColor Green
    Write-Host ""
    $features | ForEach-Object { Write-Host $_ }
    Write-Host ""
}

if ($changes.Count -gt 0) {
    Write-Host "### Changed" -ForegroundColor Yellow
    Write-Host ""
    $changes | ForEach-Object { Write-Host $_ }
    Write-Host ""
}

if ($fixes.Count -gt 0) {
    Write-Host "### Fixed" -ForegroundColor Magenta
    Write-Host ""
    $fixes | ForEach-Object { Write-Host $_ }
    Write-Host ""
}

if ($other.Count -gt 0) {
    Write-Host "### Other" -ForegroundColor Gray
    Write-Host ""
    $other | ForEach-Object { Write-Host $_ }
    Write-Host ""
}

Write-Host "($($commits.Count) commits)" -ForegroundColor Gray
