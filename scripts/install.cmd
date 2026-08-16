@echo off
:: ============================================
:: Fallout 4 - Install
:: ============================================
:: Thin wrapper - install body lives in cameraunlock-core/scripts/install-body-asi.cmd.

:: --- CONFIG BLOCK ---
set "GAME_ID=fallout-4"
set "MOD_DISPLAY_NAME=Fallout 4 Head Tracking"
:: HeadTracking.ini is deliberately NOT deployed here. The install body copies
:: every MOD_DLLS entry with "copy /y", so listing the config would overwrite the
:: player's tuned settings on every update. The mod writes the file with defaults
:: on first launch when it is absent, so nothing is lost by leaving it out.
set "MOD_DLLS=Fallout4HeadTracking.asi"
set "MOD_INTERNAL_NAME=Fallout4HeadTracking"
set "MOD_VERSION=0.0.0"
set "STATE_FILE=.headtracking-state.json"
set "FRAMEWORK_TYPE=ASILoader"
:: Fallout4.exe does NOT import dinput8.dll (Skyrim SE does - that is where the
:: original value came from). It does import dxgi.dll, so that is the proxy slot
:: Ultimate ASI Loader has to occupy here.
set "ASI_LOADER_NAME=dxgi.dll"
set "MOD_CONTROLS=Controls (nav-cluster or Ctrl+Shift+letter chord):&echo   Home / Ctrl+Shift+T - Recenter&echo   End  / Ctrl+Shift+Y - Toggle tracking&echo   PgUp / Ctrl+Shift+G - Cycle tracking mode&echo   PgDn / Ctrl+Shift+H - Toggle world/local yaw&echo          Ctrl+Shift+U - Next tracker source"
:: --- END CONFIG BLOCK ---

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
