@echo off
:: ============================================
:: Fallout 4 - Install
:: ============================================
:: Thin wrapper - install body lives in cameraunlock-core/scripts/install-body-asi.cmd.

:: --- CONFIG BLOCK ---
set "GAME_ID=fallout-4"
set "MOD_DISPLAY_NAME=Fallout 4 Head Tracking"
:: No config is deployed. The mod creates CameraUnlock.ini at first launch,
:: importing HeadTracking.ini from an earlier version once, so a copy placed here
:: would stop that import on an update, and MOD_DLLS's "copy /y" would overwrite
:: the player's settings on every install.
set "MOD_DLLS=Fallout4HeadTracking.asi"
set "MOD_INTERNAL_NAME=Fallout4HeadTracking"
set "MOD_VERSION=0.0.0"
set "STATE_FILE=.headtracking-state.json"
set "FRAMEWORK_TYPE=ASILoader"
:: Fallout4.exe does NOT import dinput8.dll (Skyrim SE does - that is where the
:: original value came from). It does import dxgi.dll, so that is the proxy slot
:: Ultimate ASI Loader has to occupy here.
set "ASI_LOADER_NAME=dxgi.dll"
:: Files copied only when they are not already there, so an upgrade keeps
:: whatever the user tuned. Listing an .ini in MOD_DLLS instead puts it through
:: the unconditional copy and resets every key on every update.
set "MOD_SEED_FILES="
set "MOD_CONTROLS=Controls (nav-cluster or Ctrl+Shift+letter chord):&echo   End  / Ctrl+Shift+Y - Toggle tracking&echo   PgUp / Ctrl+Shift+G - Cycle tracking mode&echo   PgDn / Ctrl+Shift+H - Toggle world/local yaw&echo          Ctrl+Shift+U - Next tracker source"
:: Not used by this mod. Set blank so a value another mod's wrapper left in
:: the same console does not reach the body.
set "ASI_SUBDIR="
set "ASI_LOADER_VERSION="
:: --- END CONFIG BLOCK ---

:: Pin delayed expansion off before `%*` is expanded on the `call` below.
:: Under `cmd /V:ON`, or with DelayedExpansion=1 in
:: HKCU\Software\Microsoft\Command Processor, cmd.exe eats a `!` out of the
:: expanded line, and a real game path like C:\Games\Oh! My Game reaches the
:: body already mangled. The body pins expansion off at its own outer scope
:: too, but that is one `call` too late to save the argument it was handed.
setlocal disabledelayedexpansion

set "WRAPPER_DIR=%~dp0"
set "_BODY=%WRAPPER_DIR%shared\install-body-asi.cmd"
if not exist "%_BODY%" set "_BODY=%WRAPPER_DIR%..\cameraunlock-core\scripts\install-body-asi.cmd"
if not exist "%_BODY%" (
    echo ERROR: install-body-asi.cmd not found in shared\ or ..\cameraunlock-core\scripts\.
    echo If this is a release ZIP, re-download it from GitHub ^(corrupt installer^).
    echo If this is the dev tree, run: git submodule update --init --recursive
    exit /b 1
)
call "%_BODY%" %*
exit /b %errorlevel%