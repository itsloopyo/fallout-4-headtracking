// SPDX-License-Identifier: MIT

#include "pch.h"

#include "core/session_end.h"
#include "core/logging.h"
#include "hooks/hook_slot.h"

#include <atomic>

namespace Fallout4HT {
namespace {

// Why this is a hook on NtTerminateProcess, of all things:
//
// A log that simply stops cannot be read. A player who quit and a player whose
// game died leave byte-identical evidence, so every "it crashed" report has to
// begin by establishing whether there was a crash at all - two of them arrived
// that way and neither could be answered.
//
// The obvious home for a clean-exit line is DLL_PROCESS_DETACH, which the
// loader runs on a normal exit and skips when a process is killed. Fallout 4
// never runs it. Measured rather than assumed: a detach handler writing through
// its own fresh file handle, so that a closed log could not explain a missing
// line, produced no file at all after a clean quit. The engine terminates
// rather than unwinds.
//
// kernel32's TerminateProcess is not it either - that hook arms and never
// fires. The call the engine actually makes is ntdll!NtTerminateProcess, which
// is also where kernel32!TerminateProcess and RtlExitUserProcess both end up,
// so one hook here covers every way out.
//
// A crash does not come through here. The process is torn down from outside by
// WerFault, so the line is absent exactly when it should be.

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
            "session ended through the game's own quit path - a log without"
            " this line ended in a crash or was killed from outside");
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
        Log::Line("session-end marker armed - a clean quit will say so in this log");
    }
}

} // namespace Fallout4HT
