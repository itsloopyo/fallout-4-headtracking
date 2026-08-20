// SPDX-License-Identifier: MIT

#include "pch.h"
#include "game_state.h"
#include "core/logging.h"
#include "core/seh_guard.h"
#include "game/fallout4_types.h"

#include <cameraunlock/memory/pattern_scanner.h>
#include <cameraunlock/memory/rtti_vtable.h>

namespace Fallout4HT {
namespace {

// The targeting menu leaves no camera state to read, so the mod asks the VATS
// singleton itself. RTTI finds the class on any build and its vtable pointer
// sits in .data exactly once, which locates the object; the mode field is then
// an offset into it. That is why nothing here is pinned per build any more - a
// game patch moves the object and the mod follows it.
//
// The offset was derived by differential search (src/diagnostics/vats_probe.cpp,
// Ctrl+Shift+V in gameplay and Ctrl+Shift+X in VATS): the byte at +0x7D was the
// only survivor inside the object, holding 0 through every gameplay sample and a
// non-zero mode through every VATS one. The Pip-Boy and the pause menu leave it
// at 0, so it means VATS rather than "a menu is open".
constexpr uintptr_t kVatsModeOffset = 0x7D;

// The mode is a small enum, not a bool: measured at 1 as the menu opens and 2
// once it has settled, which is why gameplay is mode == 0 rather than a boolean
// test. A byte outside this range is not the field this was derived against,
// whatever else matched, and reading on would leave head tracking switched off
// with no explanation.
constexpr uint8_t kMaxVatsMode = 4;

std::atomic<bool> g_gateTrusted{false};

// The VATS singleton, found through its RTTI vtable. Zero means it was not
// resolved, and the targeting-menu half of the gate stays off.
uintptr_t g_vatsObject = 0;

bool FindSection(uintptr_t moduleBase, const char* wanted, uintptr_t& start, size_t& size) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(moduleBase);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(moduleBase + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        char name[9] = {};
        std::memcpy(name, section->Name, 8);
        if (std::strcmp(name, wanted) != 0) continue;
        start = moduleBase + section->VirtualAddress;
        size = section->Misc.VirtualSize;
        return true;
    }
    return false;
}

// Counts every 8-aligned slot in .data holding the vtable, and reports the last
// one. A global object has exactly one; anything else means this is not the
// shape assumed here and the address is not to be trusted.
size_t ScanDataForVtable(uintptr_t dataStart, size_t dataSize, uintptr_t vtable,
                         uintptr_t& found) {
    size_t hits = 0;
    static std::atomic<uint64_t> s_faults{0};
    __try {
        for (size_t off = 0; off + sizeof(uintptr_t) <= dataSize; off += 8) {
            if (*reinterpret_cast<const uintptr_t*>(dataStart + off) != vtable) continue;
            found = dataStart + off;
            ++hits;
        }
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "vats singleton scan", s_faults)) {
    }
    return hits;
}

void ResolveVatsSingleton(HMODULE gameModule, uintptr_t moduleBase) {
    cameraunlock::memory::VtableInfo vtInfo{};
    if (!cameraunlock::memory::FindVtableFromRTTI(gameModule, kRTTI_VATS, vtInfo, 1)) {
        Log::Line("WARN: game state: no VATS RTTI - the targeting-menu flag cannot be"
                  " anchored to the singleton");
        return;
    }

    uintptr_t dataStart = 0;
    size_t dataSize = 0;
    if (!FindSection(moduleBase, ".data", dataStart, dataSize)) {
        Log::Line("WARN: game state: no .data section - VATS singleton not resolved");
        return;
    }

    uintptr_t found = 0;
    const size_t hits = ScanDataForVtable(dataStart, dataSize, vtInfo.vtable_address, found);
    if (hits != 1) {
        Log::Line("WARN: game state: VATS vtable module+0x%llX appears in .data %zu times,"
                  " not once - singleton not resolved",
                  static_cast<unsigned long long>(vtInfo.vtable_address - moduleBase), hits);
        return;
    }

    g_vatsObject = found;
    g_gateTrusted.store(true, std::memory_order_relaxed);
    Log::Line("game state: VATS singleton at module+0x%llX (vtable module+0x%llX), mode byte"
              " at +0x%llX - head tracking steps aside for VATS so it frames and labels the"
              " target, not the head",
              static_cast<unsigned long long>(found - moduleBase),
              static_cast<unsigned long long>(vtInfo.vtable_address - moduleBase),
              static_cast<unsigned long long>(kVatsModeOffset));
}

// PlayerCamera's VATS state, resolved by RTTI. Zero means it was not found, and
// the attack-camera half of the gate stays off.
uintptr_t g_vatsCameraStateVtable = 0;

void ResolveVatsCameraState(HMODULE gameModule) {
    cameraunlock::memory::VtableInfo vtInfo{};
    if (!cameraunlock::memory::FindVtableFromRTTI(gameModule, kRTTI_VATSCameraState, vtInfo, 1)) {
        Log::Line("WARN: game state: no VATSCameraState RTTI - head tracking will follow the"
                  " VATS attack camera through the shot");
        return;
    }
    g_vatsCameraStateVtable = vtInfo.vtable_address;
    Log::Line("game state: VATS attack camera is state vtable %llX - head tracking stands down"
              " for the shot the game framed",
              static_cast<unsigned long long>(g_vatsCameraStateVtable));
}

// True while PlayerCamera is running its VATS state, which is the attack
// sequence: the targeting menu leaves the camera in whatever state it was
// already in (measured: first person and third person both unchanged through
// entry), so this says nothing about it and the .data flag covers that half.
bool IsVatsAttackCamera(void* playerCamera) {
    if (g_vatsCameraStateVtable == 0 || playerCamera == nullptr) return false;

    static std::atomic<uint64_t> s_faults{0};
    __try {
        const uintptr_t state = *reinterpret_cast<uintptr_t*>(
            reinterpret_cast<uintptr_t>(playerCamera) + TESCameraOffsets::CurrentState);
        if (state == 0) return false;
        const bool isVats = *reinterpret_cast<uintptr_t*>(state) == g_vatsCameraStateVtable;
        static std::atomic<int> s_last{-1};
        const int now = isVats ? 1 : 0;
        if (s_last.exchange(now, std::memory_order_relaxed) != now) {
            Log::Line("game state: VATS attack camera %s, head tracking %s",
                      isVats ? "started" : "ended", isVats ? "off" : "on");
        }
        return isVats;
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "vats camera state", s_faults)) {
    }
    return false;
}

} // namespace

bool GameState::Initialize() {
    HMODULE gameModule = GetModuleHandleA(GAME_EXE);
    uintptr_t moduleBase = 0;
    size_t moduleSize = 0;
    if (!gameModule ||
        !cameraunlock::memory::GetModuleRange(gameModule, moduleBase, moduleSize)) {
        Log::Line("WARN: game state: no module - VATS gate off, head tracking active"
                  " in all states");
        return false;
    }

    ResolveVatsCameraState(gameModule);
    ResolveVatsSingleton(gameModule, moduleBase);

    return true;
}

bool GameState::IsInGameplay(void* playerCamera) {
    if (IsVatsAttackCamera(playerCamera)) return false;
    if (!g_gateTrusted.load(std::memory_order_relaxed)) return true;

    static std::atomic<uint64_t> s_faults{0};
    __try {
        const uint8_t mode = *reinterpret_cast<const volatile uint8_t*>(
            g_vatsObject + kVatsModeOffset);
        if (mode > kMaxVatsMode) {
            g_gateTrusted.store(false, std::memory_order_relaxed);
            Log::Line("WARN: game state: the VATS mode byte read %u, which is outside the"
                      " range this was derived against - gate off for this session, head"
                      " tracking active in all states. Re-derive with Ctrl+Shift+V /"
                      " Ctrl+Shift+X.", mode);
            return true;
        }
        // Every change is logged. The field is read from an object found at
        // runtime, so a patch that reorders the class would show up here as a
        // mode that changes at the wrong moments rather than as silence.
        static std::atomic<int> s_last{-1};
        const int previous = s_last.exchange(mode, std::memory_order_relaxed);
        if (previous != mode) {
            Log::Line("game state: VATS mode %d -> %u, head tracking %s", previous, mode,
                      mode == 0 ? "on" : "off so VATS frames and labels the target");
        }
        return mode == 0;
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "vats mode", s_faults)) {
        g_gateTrusted.store(false, std::memory_order_relaxed);
    }
    return true;
}

uintptr_t VatsSingletonAddress() { return g_vatsObject; }

} // namespace Fallout4HT
