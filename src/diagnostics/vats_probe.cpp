// SPDX-License-Identifier: MIT

#include "pch.h"
#include "vats_probe.h"
#include "core/logging.h"
#include "core/seh_guard.h"
#include "diagnostics/frame_verdict.h"
#include "game/game_state.h"

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

// How far past the VATS singleton's address a candidate may sit and still be
// treated as part of the object. The class is nowhere near this big; the window
// is deliberately loose because the point is to see whether the flag is in the
// object at all, and the exact size is not known until it is.
constexpr uintptr_t kVatsObjectWindow = 0x1000;

// Static buffer rather than a return by value: this runs inside the __try of
// the sampler, where MSVC will not accept a type with a destructor.
const char* FormatVatsOffset(uintptr_t delta) {
    static char buffer[48];
    std::snprintf(buffer, sizeof(buffer), " (VATS singleton + 0x%llX)",
                  static_cast<unsigned long long>(delta));
    return buffer;
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

        // Whether the game thread was running when the sample was taken, so a
        // mislabelled sample can be spotted afterwards rather than quietly
        // poisoning the search. VATS freezes the game update; gameplay does not.
        unsigned long long ticks = 0, sinceMs = 0;
        GetCameraTickLiveness(ticks, sinceMs);

        const size_t alive = CountAlive();
        Log::Line("vats probe: %d gameplay + %d VATS samples -> %zu candidate byte(s)"
                  " (last camera tick %llu ms ago, game %s)",
                  g_gameplaySamples, g_vatsSamples, alive, sinceMs,
                  sinceMs >= 40 ? "FROZEN" : "running");

        // A mode flag holds a small value - 0 in gameplay, 1 or 2 in VATS - so
        // the survivors worth looking at are the ones shaped like one. Counters
        // and pointers survive the alternation just as well and swamp the list.
        if (g_vatsSamples >= 2) {
            // A candidate inside the VATS singleton is worth more than one that
            // is not: an offset into an object found by RTTI reads the same on
            // every build, while a raw .data address has to be re-derived after
            // every patch. So those are listed in full and the rest are capped.
            const uintptr_t vats = VatsSingletonAddress();
            int shown = 0;
            size_t flagLike = 0;
            size_t inObject = 0;
            for (size_t i = 0; i < g_dataSize; ++i) {
                if (!g_alive[i]) continue;
                if (g_inGameplay[i] > 4 || g_inVats[i] > 4) continue;
                ++flagLike;
                const uintptr_t addr = g_dataStart + i;
                const bool nearVats = vats != 0 && addr >= vats && addr < vats + kVatsObjectWindow;
                if (nearVats) ++inObject;
                if (!nearVats && shown >= 30) continue;
                if (!nearVats) ++shown;
                Log::Line("  flag-shaped candidate module+0x%llX%s: gameplay %02X, VATS %02X",
                          static_cast<unsigned long long>(addr - moduleBase),
                          nearVats ? FormatVatsOffset(addr - vats) : "",
                          g_inGameplay[i], g_inVats[i]);
            }
            Log::Line("  %zu of %zu candidates are flag-shaped, %zu of them inside the VATS"
                      " singleton", flagLike, alive, inObject);
        }
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "vats probe", s_faults)) {
    }
}

void ProbeVats() { ProbeVatsSample(false); }
void ProbeVatsInVats() { ProbeVatsSample(true); }

} // namespace Fallout4HT
