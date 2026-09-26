// SPDX-License-Identifier: MIT

#include "pch.h"
#include "core/mod.h"
#include "core/logging.h"
#include "core/path_utils.h"
#include "ui/game_window.h"
#include "game/game_state.h"
#include "hooks/player_hook.h"
#include "diagnostics/frame_verdict.h"

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
// the engine has stood itself up. The game's own window is the first thing that
// says it has, so that is what is waited on rather than a fixed sleep - a sleep
// is only ever tuned for the machine it was tuned on, and on a slower one every
// lookup that follows it runs against an engine that is not there yet.
//
// The cap is generous because overshooting costs nothing (the wait ends the
// moment the window appears) while undershooting costs the whole session.
constexpr unsigned kGameWindowWaitMillis = 120000;

// Settle time after the window appears, before anything is resolved. The window
// exists slightly before the systems behind it do.
constexpr DWORD kPostWindowSettleMillis = 2000;

// How often the VATS singleton scan is retried until it lands. It scans the
// whole .data section, so it runs here rather than on any game thread.
constexpr DWORD kVatsRetryMillis = 5000;

unsigned __stdcall VatsRetryThread(void*) {
    for (;;) {
        if (Fallout4HT::GameState::EnsureVatsSingletonResolved()) return 0;
        Sleep(kVatsRetryMillis);
    }
}

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

    // The core log opens truncating, so a crash-then-relaunch would destroy the
    // session worth reading. Keep exactly one previous generation: the crash
    // handler writes its report into the log the player is asked to send, and
    // they relaunch before sending it.
    const std::wstring logPath = GetModulePathW("HeadTracking.log");
    const bool hadPriorLog = GetFileAttributesW(logPath.c_str()) != INVALID_FILE_ATTRIBUTES;
    const bool rotated = MoveFileExW(logPath.c_str(),
                                     GetModulePathW("HeadTracking.prev.log").c_str(),
                                     MOVEFILE_REPLACE_EXISTING) != FALSE;
    const DWORD rotateErr = GetLastError();

    Log::Open(logPath);
    if (hadPriorLog && !rotated) {
        Log::Line("WARN: could not rotate the previous log to HeadTracking.prev.log (error %lu) - "
                  "that file is from an older session", rotateErr);
    }
    cameraunlock::diagnostics::InstallCrashHandler();
    Log::Line("Fallout 4 Head Tracking v%s attached to game process", VERSION);

    if (!WaitForGameWindow(kGameWindowWaitMillis)) {
        Log::Line("WARN: no game window after %u ms - carrying on, but anything that needs"
                  " the window (reticle placement, windowed centring) will pick it up late",
                  kGameWindowWaitMillis);
    }
    Sleep(kPostWindowSettleMillis);

    CenterGameWindow();

    if (!Mod::Instance().Initialize()) {
        Log::Line("ERROR: Mod initialization failed");
        return 1;
    }

    Log::Line("Fallout 4 Head Tracking v%s loaded successfully", VERSION);

    // Nothing resolved at runtime gets one attempt. The VATS singleton may not
    // exist yet however long the wait above was, so it is retried until it does.
    if (HANDLE vatsThread = reinterpret_cast<HANDLE>(
            _beginthreadex(nullptr, 0, VatsRetryThread, nullptr, 0, nullptr))) {
        CloseHandle(vatsThread);
    }

    // Never returns; keeps windowed mode centred for the life of the process.
    WatchWindowPlacement();
    return 0;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            g_initThreadHandle = reinterpret_cast<HANDLE>(
                _beginthreadex(nullptr, 0, InitThread, nullptr, 0, nullptr));
            break;

        case DLL_PROCESS_DETACH:
            // We're holding the loader lock here. Joining a live thread or
            // running MinHook teardown under the lock can deadlock if any of
            // them touches LoadLibrary/GetModuleHandle, and is pointless on
            // process teardown because the OS will reclaim everything.
            if (g_initThreadHandle) {
                CloseHandle(g_initThreadHandle);
                g_initThreadHandle = nullptr;
            }
            // When the game leaves through ExitProcess, the CRT destroys our
            // globals after this returns, and a std::thread destroyed while
            // still joinable calls std::terminate - a fast-fail crash on quit.
            // lpReserved is non-null only on process termination, where every
            // other thread has already been ended, so these joins return at
            // once and cannot deadlock on the loader lock.
            if (lpReserved != nullptr) {
                Fallout4HT::StopFrameVerdictReporter();
                Fallout4HT::StopPauseWatchdog();
            }
            // Do not put a "session ended" line here. Most quits never reach
            // this case: a detach handler writing through its own fresh file
            // handle, so that a closed log could not explain a missing line,
            // produced no file at all after a clean quit. The engine usually
            // terminates rather than unwinds. The marker hooks
            // ntdll!NtTerminateProcess instead - see core/session_end.cpp.
            Fallout4HT::Log::Close();
            break;
    }
    return TRUE;
}
