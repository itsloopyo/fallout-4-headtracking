// SPDX-License-Identifier: MIT

#include "pch.h"
#include "activation_hook.h"
#include "collision_math.h"
#include "hook_slot.h"
#include "core/logging.h"
#include "core/seh_guard.h"
#include "game/game_setting.h"

namespace Fallout4HT {
namespace {

constexpr const char* kLengthSetting = "fActivatePickLength:Interface";
constexpr const char* kRadiusSetting = "fActivatePickRadius:Interface";

// The caster's update, as its one caller passes it: the caster, the player's
// cell, the ray's origin and unit direction, and a collision layer.
typedef void* (__fastcall *CasterUpdate_t)(void* caster, void* cell, const NiPoint3* origin,
                                           const NiPoint3* direction, int layer);
CasterUpdate_t g_originalUpdate = nullptr;
HookSlot g_updateHook;

std::mutex g_referenceMutex;
AimReference g_reference{};
bool g_haveReference = false;

bool CleanRayFor(const NiPoint3* origin, const NiPoint3* direction, NiPoint3& cleanOrigin,
                 NiPoint3& cleanDirection) {
    AimReference reference;
    if (!GetAimReference(reference)) return false;
    static std::atomic<uint64_t> s_faults{0};
    __try {
        if (!IsViewAxisRay(&origin->x, &direction->x, &reference.trackedEye.x, &reference.trackedForward.x)) {
            static uint64_t s_lastLogMs = 0;
            const uint64_t nowMs = GetTickCount64();
            if (nowMs - s_lastLogMs >= 2000) {
                s_lastLogMs = nowMs;
                const float dx = origin->x - reference.trackedEye.x, dy = origin->y - reference.trackedEye.y,
                            dz = origin->z - reference.trackedEye.z;
                const float cosine = direction->x * reference.trackedForward.x +
                                     direction->y * reference.trackedForward.y +
                                     direction->z * reference.trackedForward.z;
                Log::Line("activation: a ray starting %.2f units from the tracked eye, %.3f deg off the tracked"
                          " view axis, was left as the game cast it",
                          sqrtf(dx * dx + dy * dy + dz * dz),
                          acosf(cosine > 1.0f ? 1.0f : cosine < -1.0f ? -1.0f : cosine) * 57.29578f);
            }
            return false;
        }
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "activation ray", s_faults)) {
        return false;
    }
    cleanOrigin = reference.cleanEye;
    cleanDirection = reference.cleanForward;
    return true;
}

void* __fastcall CasterUpdateHook(void* caster, void* cell, const NiPoint3* origin,
                                  const NiPoint3* direction, int layer) {
    NiPoint3 cleanOrigin;
    NiPoint3 cleanDirection;
    if (CleanRayFor(origin, direction, cleanOrigin, cleanDirection)) {
        static bool s_logged = false;
        if (!s_logged) {
            s_logged = true;
            Log::Line("activation: the game cast from the head-tracked camera, now casting from the body's eye"
                      " along the body's aim");
        }
        return g_originalUpdate(caster, cell, &cleanOrigin, &cleanDirection, layer);
    }
    return g_originalUpdate(caster, cell, origin, direction, layer);
}

// The address of a setting's value, or 0 unless exactly one Setting carries the name.
uintptr_t FindSettingValue(uintptr_t moduleBase, const char* settingName) {
    const char* name = FindSettingName(moduleBase, settingName);
    uintptr_t dataStart = 0;
    size_t dataSize = 0;
    if (name == nullptr || !FindSection(moduleBase, ".data", dataStart, dataSize)) return 0;

    const uintptr_t wanted = reinterpret_cast<uintptr_t>(name);
    uintptr_t value = 0;
    size_t hits = 0;
    static std::atomic<uint64_t> s_faults{0};
    __try {
        for (size_t off = kSettingNameOffset; off + sizeof(uintptr_t) <= dataSize; off += 8) {
            if (*reinterpret_cast<const uintptr_t*>(dataStart + off) != wanted) continue;
            value = dataStart + off - kSettingNameOffset + kSettingValueOffset;
            ++hits;
        }
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "activation setting scan", s_faults)) {
        return 0;
    }
    return hits == 1 ? value : 0;
}

// The entry of the function holding `address`, from the image's own unwind
// tables. A function the compiler split has a record per fragment, each chained
// to its parent, so the chain is followed to the record that owns the prologue.
uintptr_t FunctionEntry(uintptr_t address) {
    DWORD64 imageBase = 0;
    const RUNTIME_FUNCTION* function = RtlLookupFunctionEntry(address, &imageBase, nullptr);
    for (int depth = 0; function != nullptr && depth < 16; ++depth) {
        const auto* unwind = reinterpret_cast<const uint8_t*>(imageBase + function->UnwindData);
        const uint8_t flags = unwind[0] >> 3;
        if ((flags & UNW_FLAG_CHAININFO) == 0) return imageBase + function->BeginAddress;
        const uint8_t codes = unwind[2];
        function = reinterpret_cast<const RUNTIME_FUNCTION*>(unwind + 4 + ((codes + 1) & ~1) * 2);
    }
    return 0;
}

constexpr int kMaxReaders = 32;

struct Readers {
    uintptr_t entries[kMaxReaders];
    int count;
};

// Every function with a rip-relative operand that resolves to `target`. The
// displacement ends the instruction, or is followed by a one or four byte
// immediate.
void FindReaders(const TextSection& text, uintptr_t target, Readers& out) {
    out.count = 0;
    static std::atomic<uint64_t> s_faults{0};
    __try {
        for (size_t off = 0; off + 4 <= text.size; ++off) {
            const uintptr_t next = text.start + off + 4;
            const uintptr_t resolved = next + *reinterpret_cast<const int32_t*>(text.start + off);
            if (resolved != target && resolved + 1 != target && resolved + 4 != target) continue;
            const uintptr_t entry = FunctionEntry(text.start + off);
            if (entry == 0) continue;
            bool known = false;
            for (int i = 0; i < out.count; ++i) known = known || out.entries[i] == entry;
            if (!known && out.count < kMaxReaders) out.entries[out.count++] = entry;
        }
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "activation reader scan", s_faults)) {
        out.count = 0;
    }
}

}  // namespace

void SetAimReference(const AimReference& reference) {
    std::lock_guard<std::mutex> lock(g_referenceMutex);
    g_reference = reference;
    g_haveReference = true;
}

void ClearAimReference() {
    std::lock_guard<std::mutex> lock(g_referenceMutex);
    g_haveReference = false;
}

bool GetAimReference(AimReference& out) {
    std::lock_guard<std::mutex> lock(g_referenceMutex);
    if (!g_haveReference) return false;
    out = g_reference;
    return true;
}

bool InstallActivationHook(uintptr_t moduleBase, const TextSection& text) {
    const uintptr_t length = FindSettingValue(moduleBase, kLengthSetting);
    const uintptr_t radius = FindSettingValue(moduleBase, kRadiusSetting);
    if (length == 0 || radius == 0) {
        Log::Line("ERROR: activation: %s or %s is not a setting of this build - what can be activated follows"
                  " the view, not the crosshair", kLengthSetting, kRadiusSetting);
        return false;
    }

    Readers lengthReaders;
    Readers radiusReaders;
    FindReaders(text, length, lengthReaders);
    FindReaders(text, radius, radiusReaders);
    uintptr_t caster = 0;
    int both = 0;
    for (int i = 0; i < lengthReaders.count; ++i) {
        for (int j = 0; j < radiusReaders.count; ++j) {
            if (lengthReaders.entries[i] != radiusReaders.entries[j]) continue;
            caster = lengthReaders.entries[i];
            ++both;
        }
    }
    if (both != 1) {
        Log::Line("ERROR: activation: %d functions read both the pick length (%d readers) and the pick radius"
                  " (%d readers), not one - what can be activated follows the view, not the crosshair",
                  both, lengthReaders.count, radiusReaders.count);
        return false;
    }

    Log::Line("activation: caster update at RVA 0x%llX", static_cast<unsigned long long>(caster - moduleBase));
    if (!g_updateHook.Install(reinterpret_cast<void*>(caster), reinterpret_cast<void*>(&CasterUpdateHook),
                              reinterpret_cast<void**>(&g_originalUpdate), "activation caster update")) {
        return false;
    }
    Log::Line("activation hook installed - what can be activated follows the crosshair");
    return true;
}

void RemoveActivationHook() {
    g_updateHook.Remove();
}

} // namespace Fallout4HT
