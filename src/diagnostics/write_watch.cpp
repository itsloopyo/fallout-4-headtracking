// SPDX-License-Identifier: MIT

#include "pch.h"
#include "write_watch.h"
#include "core/logging.h"
#include "hooks/module_scan.h"

#include <tlhelp32.h>
#include <psapi.h>

namespace Fallout4HT {
namespace {

// Enough to see the writer and its immediate neighbours without filling the log
// if the address turns out to be written from a dozen places.
constexpr int kMaxReports = 12;

PVOID g_handler = nullptr;
uintptr_t g_moduleBase = 0;
uintptr_t g_moduleEnd = 0;
uintptr_t g_seen[kMaxReports] = {};
std::atomic<int> g_seenCount{0};

// Walk back from an instruction to the start of the function containing it.
// Compilers pad between functions with int3 (0xCC), so the first run of padding
// below the address is the previous function's tail. Approximate, but it turns a
// raw instruction address into something that can be pattern-matched.
uintptr_t FunctionStartFrom(uintptr_t instruction) {
    for (uintptr_t p = instruction; p > instruction - 0x2000 && p > g_moduleBase; --p) {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(p - 1);
        if (bytes[0] == 0xCC) return p;
    }
    return 0;
}

void SetDebugRegistersOnThread(HANDLE thread, uintptr_t address, int sizeBytes, bool self) {
    CONTEXT ctx{};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (!self) SuspendThread(thread);
    if (GetThreadContext(thread, &ctx)) {
        ctx.Dr0 = address;
        // DR7: L0 enables the breakpoint, RW0=01 makes it break on writes, and
        // LEN0 encodes the width (00=1 byte, 01=2, 11=4, 10=8).
        const uint64_t lenBits = (sizeBytes == 8) ? 0b10ull
                               : (sizeBytes == 4) ? 0b11ull
                               : (sizeBytes == 2) ? 0b01ull : 0b00ull;
        ctx.Dr7 = (ctx.Dr7 & ~0xF0000ull) | (0b01ull << 16) | (lenBits << 18) | 0x1ull;
        ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        SetThreadContext(thread, &ctx);
    }
    if (!self) ResumeThread(thread);
}

void ArmAllThreads(uintptr_t address, int sizeBytes) {
    const DWORD self = GetCurrentThreadId();
    const DWORD pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;

    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    int armed = 0;
    if (Thread32First(snap, &entry)) {
        do {
            if (entry.th32OwnerProcessID != pid) continue;
            if (entry.th32ThreadID == self) {
                SetDebugRegistersOnThread(GetCurrentThread(), address, sizeBytes, true);
                ++armed;
                continue;
            }
            HANDLE thread = OpenThread(
                THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME,
                FALSE, entry.th32ThreadID);
            if (!thread) continue;
            SetDebugRegistersOnThread(thread, address, sizeBytes, false);
            CloseHandle(thread);
            ++armed;
        } while (Thread32Next(snap, &entry));
    }
    CloseHandle(snap);
    Log::Line("write watch: armed on %d threads", armed);
}

LONG CALLBACK WatchHandler(EXCEPTION_POINTERS* info) {
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    // DR6 bit 0 says our DR0 breakpoint is the one that fired, rather than a
    // single-step from something else.
    if ((info->ContextRecord->Dr6 & 0x1) == 0) return EXCEPTION_CONTINUE_SEARCH;
    info->ContextRecord->Dr6 = 0;

    const uintptr_t rip = static_cast<uintptr_t>(info->ContextRecord->Rip);
    if (rip >= g_moduleBase && rip < g_moduleEnd) {
        bool known = false;
        const int count = g_seenCount.load(std::memory_order_acquire);
        for (int i = 0; i < count && i < kMaxReports; ++i) {
            if (g_seen[i] == rip) { known = true; break; }
        }
        if (!known && count < kMaxReports) {
            g_seen[count] = rip;
            g_seenCount.store(count + 1, std::memory_order_release);
            const uintptr_t start = FunctionStartFrom(rip);
            Log::Line("WRITE WATCH: 0x%llX writes it (function starts near RVA 0x%llX)",
                      static_cast<unsigned long long>(rip - g_moduleBase),
                      static_cast<unsigned long long>(start ? start - g_moduleBase : 0));
            if (count + 1 >= kMaxReports) Log::Line("write watch: report limit reached");
        }
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}

} // namespace

void ArmWriteWatch(uintptr_t address, int sizeBytes) {
    if (address == 0) {
        Log::Line("write watch: nothing to watch");
        return;
    }

    HMODULE game = GetModuleHandleA(GAME_EXE);
    if (!game) return;
    MODULEINFO mi{};
    if (!GetModuleInformation(GetCurrentProcess(), game, &mi, sizeof(mi))) return;
    g_moduleBase = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll);
    g_moduleEnd = g_moduleBase + mi.SizeOfImage;

    g_seenCount.store(0);

    if (!g_handler) g_handler = AddVectoredExceptionHandler(1, WatchHandler);
    ArmAllThreads(address, sizeBytes);
    Log::Line("write watch: watching 0x%llX (%d bytes) - reproduce now",
              static_cast<unsigned long long>(address), sizeBytes);
}

} // namespace Fallout4HT
