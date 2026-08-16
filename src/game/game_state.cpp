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

// Everything else this mod pins is found at runtime by RTTI or by pattern, so a
// game patch moves it for free. The VATS flag cannot be: it is a byte in .data
// with no name, no vtable and no code signature of its own, so it is pinned per
// build and gated on the PE fingerprint the way the doctrine requires.
//
// TimeDateStamp, SizeOfImage and CheckSum together identify one shipped EXE.
// On no match the gate simply stays off and head tracking behaves as it did
// before it existed - the pause watchdog still puts the head pose away when the
// game stops updating, so VATS keeps the right VIEW and only loses the overlay
// alignment. Reading a byte at a stale address is the one outcome worth
// avoiding: it could read non-zero for ever and leave tracking silently off.
struct BuildProfile {
    const char* name;
    uint32_t timeDateStamp;
    uint32_t sizeOfImage;
    uint32_t checkSum;
    uintptr_t vatsActiveOffset;
};

// Found by differential search, in src/diagnostics/vats_probe.cpp: samples of
// .data taken alternately in gameplay and in VATS, keeping only the bytes that
// held one value every time in the first and a different value every time in the
// second. Six cycles took 16.3 million bytes to 108 flag-shaped survivors, and
// walking the Pip-Boy and the pause menu separated the ones that mean VATS from
// the ones that mean "a menu is open". Ctrl+Shift+V / Ctrl+Shift+X re-run it.
//
// The date in each name is the EXE's link date, so `pixi run check-fingerprint`
// prints a line that can be pasted straight in. Newest first.
const BuildProfile kKnownProfiles[] = {
    { "steam-win64-20260417", 0x69E2A744, 0x04244000, 0x034C0846, 0x326C129 },
};

uintptr_t g_vatsFlag = 0;
std::atomic<bool> g_gateTrusted{false};

// PlayerCamera's VATS state, resolved by RTTI. Zero means it was not found, and
// the attack-camera half of the gate stays off.
uintptr_t g_vatsCameraStateVtable = 0;

bool ReadFingerprint(uintptr_t moduleBase, uint32_t& stamp, uint32_t& size, uint32_t& sum) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(moduleBase);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(moduleBase + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    stamp = nt->FileHeader.TimeDateStamp;
    size = nt->OptionalHeader.SizeOfImage;
    sum = nt->OptionalHeader.CheckSum;
    return true;
}

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

    uint32_t stamp = 0, size = 0, sum = 0;
    if (!ReadFingerprint(moduleBase, stamp, size, sum)) {
        Log::Line("WARN: game state: unreadable PE header - VATS gate off");
        return false;
    }

    for (const BuildProfile& profile : kKnownProfiles) {
        if (profile.timeDateStamp != stamp || profile.sizeOfImage != size ||
            profile.checkSum != sum) {
            continue;
        }
        // A profile can be added the moment a patch is spotted, before anyone
        // has re-derived the flag for it. Zero means exactly that, and leaves
        // the gate off rather than reading the DOS header.
        if (profile.vatsActiveOffset == 0) {
            Log::Line("game state: build %s is known but its VATS flag has not been"
                      " re-derived yet - gate off. Head tracking runs normally; VATS will"
                      " frame the target but its body-part overlay will sit where the head"
                      " was looking. Re-derive with Ctrl+Shift+V / Ctrl+Shift+X.",
                      profile.name);
            return true;
        }
        if (profile.vatsActiveOffset >= moduleSize) {
            Log::Line("WARN: game state: %s has a VATS offset outside the module - gate off",
                      profile.name);
            return false;
        }
        g_vatsFlag = moduleBase + profile.vatsActiveOffset;
        g_gateTrusted.store(true, std::memory_order_relaxed);
        Log::Line("game state: build %s, VATS flag at module+0x%llX - head tracking"
                  " steps aside for VATS so it frames and labels the target, not the head",
                  profile.name, static_cast<unsigned long long>(profile.vatsActiveOffset));
        return true;
    }

    Log::Line("game state: this Fallout 4 is not a build the VATS gate knows"
              " (TimeDateStamp 0x%08X, SizeOfImage 0x%08X, CheckSum 0x%08X). Head tracking"
              " runs normally; VATS will frame the target but its body-part overlay will"
              " sit where the head was looking. Re-derive with Ctrl+Shift+V / Ctrl+Shift+X.",
              stamp, size, sum);
    return true;
}

bool GameState::IsInGameplay(void* playerCamera) {
    if (IsVatsAttackCamera(playerCamera)) return false;
    if (!g_gateTrusted.load(std::memory_order_relaxed)) return true;

    static std::atomic<uint64_t> s_faults{0};
    __try {
        const uint8_t value = *reinterpret_cast<const volatile uint8_t*>(g_vatsFlag);
        // A boolean that is neither 0 nor 1 is not the flag this was pinned to,
        // whatever the fingerprint said. Standing down beats leaving head
        // tracking switched off for the rest of the session with no explanation.
        if (value > 1) {
            g_gateTrusted.store(false, std::memory_order_relaxed);
            Log::Line("WARN: game state: the VATS flag read %u, which is not a boolean -"
                      " gate off for this session, head tracking active in all states",
                      value);
            return true;
        }
        // Every change is logged. A byte found by differential search can
        // correlate perfectly in one session and mean something else after a
        // save is loaded, and a gate that silently sticks would switch head
        // tracking off for the rest of the session with no explanation.
        static std::atomic<int> s_last{-1};
        const int previous = s_last.exchange(value, std::memory_order_relaxed);
        if (previous != value) {
            Log::Line("game state: VATS flag %d -> %u, head tracking %s", previous, value,
                      value == 0 ? "on" : "off so VATS frames and labels the target");
        }
        return value == 0;
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "vats flag", s_faults)) {
        g_gateTrusted.store(false, std::memory_order_relaxed);
    }
    return true;
}

} // namespace Fallout4HT
