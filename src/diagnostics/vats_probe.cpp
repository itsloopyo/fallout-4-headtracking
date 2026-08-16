// SPDX-License-Identifier: MIT

#include "pch.h"
#include "vats_probe.h"
#include "core/logging.h"
#include "core/seh_guard.h"

#include <cameraunlock/memory/pattern_scanner.h>

#include <vector>

namespace Fallout4HT {
namespace {

// Differential search for the byte that says "VATS is active".
//
// A single before/after diff is useless here: 40,000 bytes of .data move between
// any two moments in a running game, and 4,700 of them happen to come back. What
// a real flag does that churn does not is hold ONE value every time the game is
// in gameplay and a DIFFERENT one every time VATS is open. So samples are taken
// alternately and a byte survives only while it keeps that promise; a few cycles
// takes the candidate set from thousands to a handful.
std::vector<uint8_t> g_inGameplay;
std::vector<uint8_t> g_inVats;
std::vector<uint8_t> g_alive;      // 1 while the byte still matches the pattern
uintptr_t g_dataStart = 0;
size_t g_dataSize = 0;
int g_gameplaySamples = 0;
int g_vatsSamples = 0;

bool FindData(uintptr_t moduleBase) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(moduleBase);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(moduleBase + dos->e_lfanew);
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        char name[9] = {};
        std::memcpy(name, section->Name, 8);
        if (std::strcmp(name, ".data") != 0) continue;
        g_dataStart = moduleBase + section->VirtualAddress;
        g_dataSize = section->Misc.VirtualSize;
        return true;
    }
    return false;
}

size_t CountAlive() {
    size_t n = 0;
    for (size_t i = 0; i < g_alive.size(); ++i) n += g_alive[i];
    return n;
}

} // namespace

void ProbeVatsSample(bool inVats) {
    HMODULE gameModule = GetModuleHandleA(GAME_EXE);
    if (!gameModule) return;
    uintptr_t moduleBase = 0;
    size_t moduleSize = 0;
    if (!cameraunlock::memory::GetModuleRange(gameModule, moduleBase, moduleSize)) return;
    if (g_dataStart == 0 && !FindData(moduleBase)) {
        Log::Line("vats probe: no .data section");
        return;
    }

    static std::atomic<uint64_t> s_faults{0};
    __try {
        const auto* live = reinterpret_cast<const uint8_t*>(g_dataStart);

        if (g_alive.empty()) {
            g_alive.assign(g_dataSize, 1);
            g_inGameplay.assign(g_dataSize, 0);
            g_inVats.assign(g_dataSize, 0);
        }

        if (!inVats) {
            if (g_gameplaySamples == 0) {
                g_inGameplay.assign(live, live + g_dataSize);
            } else {
                for (size_t i = 0; i < g_dataSize; ++i)
                    if (g_alive[i] && live[i] != g_inGameplay[i]) g_alive[i] = 0;
            }
            ++g_gameplaySamples;
        } else {
            if (g_vatsSamples == 0) {
                g_inVats.assign(live, live + g_dataSize);
                // Must actually differ from the gameplay value, or it says nothing.
                for (size_t i = 0; i < g_dataSize; ++i)
                    if (g_alive[i] && g_inVats[i] == g_inGameplay[i]) g_alive[i] = 0;
            } else {
                for (size_t i = 0; i < g_dataSize; ++i)
                    if (g_alive[i] && live[i] != g_inVats[i]) g_alive[i] = 0;
            }
            ++g_vatsSamples;
        }

        const size_t alive = CountAlive();
        Log::Line("vats probe: %d gameplay + %d VATS samples -> %zu candidate byte(s)",
                  g_gameplaySamples, g_vatsSamples, alive);

        // A mode flag holds a small value - 0 in gameplay, 1 or 2 in VATS - so
        // the survivors worth looking at are the ones shaped like one. Counters
        // and pointers survive the alternation just as well and swamp the list.
        if (g_vatsSamples >= 2) {
            int shown = 0;
            size_t flagLike = 0;
            for (size_t i = 0; i < g_dataSize; ++i) {
                if (!g_alive[i]) continue;
                if (g_inGameplay[i] > 4 || g_inVats[i] > 4) continue;
                ++flagLike;
                if (shown < 30) {
                    ++shown;
                    Log::Line("  flag-shaped candidate module+0x%llX: gameplay %02X, VATS %02X",
                              static_cast<unsigned long long>(g_dataStart + i - moduleBase),
                              g_inGameplay[i], g_inVats[i]);
                }
            }
            Log::Line("  %zu of %zu candidates are flag-shaped", flagLike, alive);
        }
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "vats probe", s_faults)) {
    }
}

void ProbeVats() { ProbeVatsSample(false); }
void ProbeVatsInVats() { ProbeVatsSample(true); }

} // namespace Fallout4HT
