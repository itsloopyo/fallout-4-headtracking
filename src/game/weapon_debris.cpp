// SPDX-License-Identifier: MIT

#include "pch.h"

#include "game/weapon_debris.h"
#include "core/logging.h"
#include "core/seh_guard.h"
#include "game/fallout4_types.h"
#include "game/game_setting.h"
#include "hooks/hook_slot.h"
#include "hooks/module_scan.h"

#include <cameraunlock/memory/rtti_vtable.h>

namespace Fallout4HT::WeaponDebris {
namespace {

constexpr const char* kSettingName = "bNVFlexEnable:NVFlex";

// The collection Fallout4Prefs.ini is read into and saved from. WriteSetting is
// slot 3 of SettingCollection<Setting>: destructor, Add, Remove, WriteSetting.
constexpr const char* kRTTI_PrefCollection = "INIPrefSettingCollection";
constexpr int kVtableIndex_WriteSetting = 3;

// The setting object once it has been cleared, null until then, and what it held.
uintptr_t g_setting = 0;
bool g_wasOn = false;

using WriteSetting_t = bool(__fastcall*)(void* collection, void* setting);
WriteSetting_t g_originalWriteSetting = nullptr;
HookSlot g_writeSettingHook;

// The game saves every setting of the collection when the player changes one in
// its settings menu. Skipping this one leaves the line in the file as the player
// has it, so the cleared value never outlives the session.
bool __fastcall WriteSettingHook(void* collection, void* setting) {
    if (reinterpret_cast<uintptr_t>(setting) == g_setting) return true;
    return g_originalWriteSetting(collection, setting);
}

} // namespace

// With weapon debris on, the game faults inside flexRelease_x64.dll (a write
// through a null pointer on one of its own worker threads) within about a minute
// of a save loading. Measured on an RTX 5080 with no hook of this mod installed,
// so it is the game's fault and not ours, but a player who has just installed a
// mod blames the mod.
void DisableForSession() {
    const uintptr_t moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(GAME_EXE));
    if (moduleBase == 0) return;

    const char* name = FindSettingName(moduleBase, kSettingName);
    if (name == nullptr) {
        Log::Line("weapon debris: this build has no %s setting, nothing to switch off", kSettingName);
        return;
    }

    uintptr_t dataStart = 0;
    size_t dataSize = 0;
    uintptr_t rdataStart = 0;
    size_t rdataSize = 0;
    if (!FindSection(moduleBase, ".data", dataStart, dataSize) ||
        !FindSection(moduleBase, ".rdata", rdataStart, rdataSize)) {
        Log::Line("ERROR: weapon debris: no .data or .rdata section - left as the game set it");
        return;
    }

    const uintptr_t wanted = reinterpret_cast<uintptr_t>(name);
    uint8_t* value = nullptr;
    size_t hits = 0;
    static std::atomic<uint64_t> s_faults{0};
    __try {
        for (size_t off = kSettingNameOffset; off + sizeof(uintptr_t) <= dataSize; off += 8) {
            if (*reinterpret_cast<const uintptr_t*>(dataStart + off) != wanted) continue;
            const uintptr_t setting = dataStart + off - kSettingNameOffset;
            const uintptr_t vtable = *reinterpret_cast<const uintptr_t*>(setting);
            uint8_t* candidate = reinterpret_cast<uint8_t*>(setting + kSettingValueOffset);
            if (vtable < rdataStart || vtable >= rdataStart + rdataSize || *candidate > 1) continue;
            value = candidate;
            ++hits;
        }
        if (hits != 1) {
            Log::Line("ERROR: weapon debris: %s matched %zu setting objects - left as the game set it",
                      kSettingName, hits);
            return;
        }
        g_wasOn = *value != 0;
        *value = 0;
        g_setting = reinterpret_cast<uintptr_t>(value) - kSettingValueOffset;
        Log::Line("weapon debris: %s for this session (the game's NVIDIA FleX debris can crash it"
                  " shortly after a save loads). Fallout4Prefs.ini is not changed",
                  g_wasOn ? "switched off" : "already off, kept off");
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "weapon debris setting", s_faults)) {
    }
}

void InstallPrefsWriteGuard() {
    if (g_setting == 0) return;
    const HMODULE gameModule = GetModuleHandleA(GAME_EXE);
    TextSection text{};
    cameraunlock::memory::VtableInfo info{};
    const bool resolved =
        gameModule != nullptr && FindTextSection(reinterpret_cast<uintptr_t>(gameModule), text) &&
        cameraunlock::memory::FindVtableFromRTTI(gameModule, kRTTI_PrefCollection, info,
                                                 kVtableIndex_WriteSetting + 1) &&
        info.vfunc_count > kVtableIndex_WriteSetting &&
        info.vfuncs[kVtableIndex_WriteSetting] >= text.start &&
        info.vfuncs[kVtableIndex_WriteSetting] < text.start + text.size;
    if (resolved &&
        g_writeSettingHook.Install(reinterpret_cast<void*>(info.vfuncs[kVtableIndex_WriteSetting]),
                                   reinterpret_cast<void*>(&WriteSettingHook),
                                   reinterpret_cast<void**>(&g_originalWriteSetting),
                                   "INIPrefSettingCollection::WriteSetting")) {
        return;
    }
    // Without the guard the game would save the cleared value the next time the
    // player changes a setting, so the setting goes back to what the player has.
    *reinterpret_cast<uint8_t*>(g_setting + kSettingValueOffset) = g_wasOn ? 1 : 0;
    g_setting = 0;
    Log::Line("ERROR: weapon debris: %s::WriteSetting could not be hooked, so the setting is back as"
              " the game loaded it (%s)", kRTTI_PrefCollection, g_wasOn ? "on" : "off");
}

} // namespace Fallout4HT::WeaponDebris
