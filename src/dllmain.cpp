// SPDX-License-Identifier: MIT

#include "pch.h"
#include "core/mod.h"
#include "core/logging.h"
#include "core/path_utils.h"
#include "ui/game_window.h"

#include <cameraunlock/diagnostics/crash_handler.h>

#include <process.h>

namespace {

HANDLE g_initThreadHandle = nullptr;

// The ASI loader injects into whatever process loaded the proxy DLL, which is
// not necessarily the game. Give the game module this long to appear before
// concluding this is something else (the launcher, a store overlay) and exiting
// without touching the log.
constexpr int kGameModuleWaitAttempts = 100;
constexpr DWORD kGameModuleWaitMillis = 100;

// The game module being present only means the loader has mapped it, not that
// the engine has stood itself up. Everything the mod hooks is built during this
// window.
constexpr DWORD kGameInitDelayMillis = 2000;

bool WaitForGameModule() {
    for (int attempt = 0; attempt <= kGameModuleWaitAttempts; ++attempt) {
        if (GetModuleHandleA(Fallout4HT::GAME_EXE)) return true;
        if (attempt == kGameModuleWaitAttempts) break;
        Sleep(kGameModuleWaitMillis);
    }
    return false;
}

unsigned __stdcall InitThread(void* lpParam) {
    (void)lpParam;

    using namespace Fallout4HT;

    // Not the game process - exit silently.
    if (!WaitForGameModule()) return 1;

    Log::Open(GetModulePathW("HeadTracking.log"));
    cameraunlock::diagnostics::InstallCrashHandler();
    Log::Line("Fallout 4 Head Tracking v%s attached to game process", VERSION);

    Sleep(kGameInitDelayMillis);

    CenterGameWindow();

    if (!Mod::Instance().Initialize()) {
        Log::Line("ERROR: Mod initialization failed");
        return 1;
    }

    Log::Line("Fallout 4 Head Tracking v%s loaded successfully", VERSION);

    // Never returns; keeps windowed mode centred for the life of the process.
    WatchWindowPlacement();
    return 0;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    (void)lpReserved;

    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            g_initThreadHandle = reinterpret_cast<HANDLE>(
                _beginthreadex(nullptr, 0, InitThread, nullptr, 0, nullptr));
            break;

        case DLL_PROCESS_DETACH:
            // We're holding the loader lock here. Joining threads or running
            // MinHook teardown under the lock can deadlock if any of them
            // touches LoadLibrary/GetModuleHandle, and is pointless on process
            // teardown because the OS will reclaim everything. Only close the
            // init-thread handle (non-blocking) and flush the log.
            if (g_initThreadHandle) {
                CloseHandle(g_initThreadHandle);
                g_initThreadHandle = nullptr;
            }
            Fallout4HT::Log::Close();
            break;
    }
    return TRUE;
}
