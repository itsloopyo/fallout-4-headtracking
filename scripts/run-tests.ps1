#!/usr/bin/env pwsh
#Requires -Version 5.1
# Build and run the native unit tests in a build tree of their own, so the
# normal build/ output (and anything the packager reads from it) is untouched.
# Every test target is built and every registered test runs - adding a target to
# CMakeLists is enough, there is no list to keep in step here.

[CmdletBinding()]
param([string]$Config = 'Debug')

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$BuildDir = Join-Path $ProjectRoot 'build-tests'

cmake -B $BuildDir -A x64 -DFALLOUT4_BUILD_TESTS=ON
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)" }

# fallout4_tests is the aggregate every fallout4_add_test hangs off, and the
# matching name prefix is what ctest selects on.
cmake --build $BuildDir --config $Config --target fallout4_tests
if ($LASTEXITCODE -ne 0) { throw "Test build failed ($LASTEXITCODE)" }

# cameraunlock-core's own tests run here too. They cover behaviour this mod
# relies on and cannot test from its own sources - notably the UDP supervisor
# reclaiming the tracker port from another game that was still running. A core
# bump that regressed that would otherwise reach a user before anything noticed.
cmake --build $BuildDir --config $Config --target cameraunlock_tests
if ($LASTEXITCODE -ne 0) { throw "Core test build failed ($LASTEXITCODE)" }

ctest --test-dir $BuildDir -C $Config --output-on-failure -R '^(fallout4_|cameraunlock_tests$)'
if ($LASTEXITCODE -ne 0) { throw "Tests failed ($LASTEXITCODE)" }

Write-Host 'All tests passed' -ForegroundColor Green
