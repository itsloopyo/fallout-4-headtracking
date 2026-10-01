// SPDX-License-Identifier: MIT

#include "pch.h"

#include "core/session_end.h"
#include "core/logging.h"
#include "hooks/hook_slot.h"

#include <atomic>

namespace Fallout4HT {
namespace {

// The engine can terminate without DLL_PROCESS_DETACH. NtTerminateProcess
// also receives fault exits, so reaching it does not establish a clean quit.

using NtTerminateProcess_t = LONG(NTAPI*)(HANDLE, LONG);

NtTerminateProcess_t g_originalNtTerminateProcess = nullptr;
HookSlot g_ntTerminateSlot;
std::atomic<bool> g_written{false};

LONG NTAPI NtTerminateProcessDetour(HANDLE process, LONG exitStatus) {
    // Our own process only. The game ending something else says nothing about
    // this session. GetCurrentProcess() is a pseudo-handle, so it is compared
    // directly as well as by id, and a null handle is the shutdown form.
    const bool self = process == nullptr || process == GetCurrentProcess() ||
                      GetProcessId(process) == GetCurrentProcessId();

    // Shutdown calls this twice (once for the other threads, once for the
    // process), so the line is written once.
    if (self && !g_written.exchange(true)) {
        // EmergencyLine, not Line: this runs while the process is being torn
        // down, where another thread may hold the log mutex and never release
        // it. It takes no lock and flushes, which is what a last line needs.
        Log::EmergencyLine(
            "session termination requested: exit status 0x%08lX",
            static_cast<unsigned long>(exitStatus));
    }
    return g_originalNtTerminateProcess(process, exitStatus);
}

} // namespace

void InstallSessionEndMarker() {
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (ntdll == nullptr) {
        Log::Line("WARN: no ntdll handle - this log cannot say whether the session"
                  " ended or crashed");
        return;
    }

    void* target = reinterpret_cast<void*>(GetProcAddress(ntdll, "NtTerminateProcess"));
    if (target == nullptr) {
        Log::Line("WARN: NtTerminateProcess not found - this log cannot say whether the"
                  " session ended or crashed");
        return;
    }

    if (g_ntTerminateSlot.Install(target, reinterpret_cast<void*>(&NtTerminateProcessDetour),
                                  reinterpret_cast<void**>(&g_originalNtTerminateProcess),
                                  "session-end marker")) {
        Log::Line("session-end marker armed - process termination status will be logged");
    }
}

} // namespace Fallout4HT
