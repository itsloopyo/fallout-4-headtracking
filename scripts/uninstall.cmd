@echo off
:: ============================================
:: Fallout 4 - Uninstall
:: ============================================
:: Thin wrapper - uninstall body lives in cameraunlock-core/scripts/uninstall-body.cmd.

:: --- CONFIG BLOCK ---
set "GAME_ID=fallout-4"
set "MOD_DISPLAY_NAME=Fallout 4 Head Tracking"
set "MOD_DLLS=Fallout4HeadTracking.asi HeadTracking.ini"
set "MOD_INTERNAL_NAME=Fallout4HeadTracking"
set "STATE_FILE=.headtracking-state.json"
set "FRAMEWORK_TYPE=ASILoader"
:: Installs before the dxgi.dll fix dropped an inert dinput8.dll in the game
:: folder (Fallout4.exe never imports dinput8, so it was never loaded).
set "LEGACY_DLLS=dinput8.dll"

:: --- Loader-specific config (leave the ones that don't apply blank) ---
set "MANAGED_SUBFOLDER="
set "ASSEMBLY_DLL="
set "MANAGED_EXTRAS="
set "ASI_LOADER_NAME=dxgi.dll"
:: --- END CONFIG BLOCK ---

set "WRAPPER_DIR=%~dp0"
set "_BODY=%WRAPPER_DIR%shared\uninstall-body.cmd"
if not exist "%_BODY%" set "_BODY=%WRAPPER_DIR%..\cameraunlock-core\scripts\uninstall-body.cmd"
if not exist "%_BODY%" (
    echo ERROR: uninstall-body.cmd not found in shared\ or ..\cameraunlock-core\scripts\.
    echo If this is a release ZIP, re-download it from GitHub ^(corrupt installer^).
    echo If this is the dev tree, run: git submodule update --init --recursive
    exit /b 1
)
call "%_BODY%" %*
exit /b %errorlevel%
