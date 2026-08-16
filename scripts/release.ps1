#!/usr/bin/env pwsh
#Requires -Version 5.1
# Automated release workflow for Fallout 4 Head Tracking.
# Steps (per ~/.claude/CLAUDE.md "Build & Release"):
#   1. Validate <version> (semver)
#   2. Verify main branch, clean tree, tag unused
#   3. Update version in manifest.json, launcher-manifest.json, install.cmd
#      MOD_VERSION, CMakeLists.txt, pixi.toml, src/core/constants.h
#      (release.yml validates manifest.json and CMakeLists.txt against the tag)
#   4. pixi run build-release (abort on failure)
#   5. Generate CHANGELOG.md from commits since the last tag
#   6. Commit "Release v<version>"
#   7. Create annotated tag v<version>
#   8. Push commits + tag (CI release workflow picks it up)
# Never destructive: refuses to run on a dirty tree or existing tag.

param(
    [Parameter(Position=0)]
    [string]$Version = "",
    # Ship a release even when there are no user-facing commits since the
    # last tag (writes a maintenance changelog entry instead of aborting).
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir    = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir   = Split-Path -Parent $scriptDir
$manifestPath = Join-Path $projectDir "manifest.json"
$launcherManifestPath = Join-Path $projectDir "launcher-manifest.json"
$installCmd   = Join-Path $scriptDir "install.cmd"
$cmakePath    = Join-Path $projectDir "CMakeLists.txt"
$pixiPath     = Join-Path $projectDir "pixi.toml"
$constantsPath = Join-Path $projectDir "src/core/constants.h"

# 'pixi run release nightly' publishes the rolling dev pre-release instead of
# cutting a versioned release.
if ($Version -eq 'nightly') {
    & (Join-Path $scriptDir 'release-nightly.ps1')
    exit $LASTEXITCODE
}

Import-Module (Join-Path $projectDir "cameraunlock-core/powershell/ReleaseWorkflow.psm1") -Force

# Mirrors New-ChangelogFromCommits' insertion so a -Force maintenance entry
# lands in the same place with the same shape.
function Add-MaintenanceChangelogEntry {
    param([string]$Path, [string]$NewVersion)
    $date = Get-Date -Format 'yyyy-MM-dd'
    $entry = "## [$NewVersion] - $date`n`n### Changed`n`n- Maintenance release (no user-facing changes).`n`n"
    $changelog = Get-Content $Path -Raw
    if ($changelog -match '(?s)(# Changelog.*?)(## \[)') {
        $changelog = $changelog -replace '(?s)(# Changelog.*?\n\n)', "`$1$entry"
    } else {
        $changelog = $changelog -replace '(?s)(# Changelog.*?\n)', "`$1$entry"
    }
    $changelog = $changelog.TrimEnd() + "`n"
    Set-Content $Path $changelog -NoNewline
}

function Get-CurrentVersion {
    $json = Get-Content $manifestPath -Raw | ConvertFrom-Json
    return $json.version
}

function Set-Version {
    param([string]$NewVersion)
    $json = Get-Content $manifestPath -Raw | ConvertFrom-Json
    $json.version = $NewVersion
    $json | ConvertTo-Json -Depth 10 | Set-Content $manifestPath -NoNewline

    # launcher-manifest.json is the file lopari reads; its mod_info.version
    # must track manifest.json.
    $launcherJson = Get-Content $launcherManifestPath -Raw | ConvertFrom-Json
    $launcherJson.mod_info.version = $NewVersion
    $launcherJson | ConvertTo-Json -Depth 10 | Set-Content $launcherManifestPath -NoNewline
}

Write-Host "=== Fallout 4 Head Tracking Release ===" -ForegroundColor Cyan
Write-Host ""

$currentVersion = Get-CurrentVersion

if ([string]::IsNullOrWhiteSpace($Version)) {
    Write-Host "Current version: " -NoNewline -ForegroundColor Yellow
    Write-Host $currentVersion -ForegroundColor White
    Write-Host ""
    Write-Host "Usage: " -NoNewline -ForegroundColor Yellow
    Write-Host "pixi run release <major|minor|patch|nightly|X.Y.Z>" -ForegroundColor White
    exit 0
}

# Step 1: resolve major/minor/patch into a concrete version (or accept literal X.Y.Z)
try {
    $Version = Resolve-ReleaseVersion -Argument $Version -CurrentVersion $currentVersion
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

$tagName = "v$Version"

# Step 2: git preconditions
$currentBranch = git rev-parse --abbrev-ref HEAD
if ($currentBranch -ne "main") {
    Write-Host "Error: Must be on 'main' branch (currently on '$currentBranch')" -ForegroundColor Red
    exit 1
}

if (-not (Test-CleanGitStatus)) {
    Write-Host "Error: Working directory has uncommitted changes" -ForegroundColor Red
    exit 1
}

if (Test-GitTagExists $tagName) {
    Write-Host "Error: Tag '$tagName' already exists" -ForegroundColor Red
    exit 1
}

Write-Host "Current version: $currentVersion" -ForegroundColor Gray
Write-Host "New version:     $Version" -ForegroundColor Green
Write-Host ""

# Step 3: changelog. This is the gate that aborts when there are no
# user-facing commits, so run it BEFORE mutating any version files or
# building - a failure here then leaves a clean tree instead of stranding a
# half-applied version bump with no tag.
Write-Host "Generating CHANGELOG from commits..." -ForegroundColor Cyan
$changelogPath = Join-Path $projectDir "CHANGELOG.md"
$hasExistingTags = git tag -l 2>$null
if (-not $hasExistingTags) {
    $date = Get-Date -Format 'yyyy-MM-dd'
    $firstEntry = "# Changelog`n`n## [$Version] - $date`n`nFirst release.`n"
    Set-Content $changelogPath $firstEntry
    Write-Host "  First release - wrote initial CHANGELOG entry" -ForegroundColor Gray
} else {
    try {
        $changelogArgs = @{
            ChangelogPath = $changelogPath
            Version       = $Version
            ArtifactPaths = @(
                "src/",
                "cameraunlock-core/",
                "scripts/install.cmd",
                "scripts/uninstall.cmd"
            )
        }
        New-ChangelogFromCommits @changelogArgs
    } catch {
        if (-not $Force) {
            Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
            Write-Host "No user-facing changes to release. Re-run with -Force for a maintenance release." -ForegroundColor Yellow
            exit 1
        }
        Write-Host "No user-facing commits since last tag - writing maintenance entry (-Force)." -ForegroundColor Yellow
        Add-MaintenanceChangelogEntry -Path $changelogPath -NewVersion $Version
    }
}

# Step 4: update version in canonical sources.
# Every file holding a version string must be bumped here. If you add another, add it
# both below AND to the `git add` list at step 6 - the release.yml workflow validates
# CMakeLists.txt, manifest.json, etc. against the tag and will fail the run if any drift.
Write-Host "Updating version to $Version..." -ForegroundColor Cyan
Set-Version $Version
(Get-Content $installCmd -Raw) -replace 'set "MOD_VERSION=.*?"', "set `"MOD_VERSION=$Version`"" | Set-Content $installCmd -NoNewline
(Get-Content $cmakePath -Raw) -replace 'project\(Fallout4HeadTracking VERSION \d+\.\d+\.\d+', "project(Fallout4HeadTracking VERSION $Version" | Set-Content $cmakePath -NoNewline
(Get-Content $pixiPath -Raw) -replace '(?m)^version = "\d+\.\d+\.\d+"', "version = `"$Version`"" | Set-Content $pixiPath -NoNewline
# constants.h VERSION is what the mod logs and shows in its startup
# notification, so a stale value here misreports the build to the user.
(Get-Content $constantsPath -Raw) -replace '(VERSION = ")\d+\.\d+\.\d+(")', "`${1}$Version`${2}" | Set-Content $constantsPath -NoNewline

# Step 5: build
Write-Host "Running 'pixi run build-release'..." -ForegroundColor Cyan
Push-Location $projectDir
try {
    & pixi run build-release
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Error: build-release failed (exit $LASTEXITCODE). Aborting release." -ForegroundColor Red
        exit 1
    }
} finally { Pop-Location }

# Step 6: commit
Write-Host "Committing version change..." -ForegroundColor Cyan
git add $manifestPath $launcherManifestPath $changelogPath $installCmd $cmakePath $pixiPath $constantsPath
git commit -m "Release v$Version"
if ($LASTEXITCODE -ne 0) { throw "git commit failed" }

# Step 7: annotated tag
Write-Host "Creating annotated tag $tagName..." -ForegroundColor Cyan
git tag -a $tagName -m "Release v$Version"
if ($LASTEXITCODE -ne 0) { throw "git tag failed" }

# Step 8: push
Write-Host "Pushing to GitHub..." -ForegroundColor Cyan
git push origin main
if ($LASTEXITCODE -ne 0) { throw "git push of main failed" }
git push origin $tagName
if ($LASTEXITCODE -ne 0) { throw "git push of tag failed" }

Write-Host ""
Write-Host "Release $tagName initiated!" -ForegroundColor Green
Write-Host "GitHub Actions will build and publish installer + nexus ZIPs." -ForegroundColor Gray
