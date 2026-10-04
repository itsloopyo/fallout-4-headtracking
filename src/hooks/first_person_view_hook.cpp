// SPDX-License-Identifier: MIT

#include "pch.h"
#include "first_person_view_hook.h"
#include "hook_slot.h"
#include "core/logging.h"
#include "core/seh_guard.h"

namespace Fallout4HT {
namespace {

typedef void (__fastcall *SetEye_t)(const NiPoint3* eye);
SetEye_t g_originalSetEye = nullptr;
HookSlot g_setEyeHook;

std::mutex g_offsetMutex;
NiPoint3 g_offset;

void __fastcall SetEyeHook(const NiPoint3* eye) {
    NiPoint3 offset;
    {
        std::lock_guard<std::mutex> lock(g_offsetMutex);
        offset = g_offset;
    }
    const NiPoint3 leaned(eye->x + offset.x, eye->y + offset.y, eye->z + offset.z);
    g_originalSetEye(&leaned);
}

// mov rcx,[rcx+0xE0] / test rcx,rcx / je / mov rax,[rcx] / jmp [rax+0x68]:
// PlayerCamera's first-person state (its first camera state) asked for its
// translation, virtual 0xD.
const uint8_t kStateTranslation[] = {
    0x48, 0x8B, 0x89, 0xE0, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC9, 0x74, 0x00,
    0x48, 0x8B, 0x01, 0x48, 0xFF, 0x60, 0x68
};
const char kStateTranslationMask[] = "xxxxxxxxxxx?xxxxxxx";

// movss xmm0,[rcx] / movss [rip+a],xmm0 / movss xmm1,[rcx+4] / movss [rip+b],xmm1
// / movss xmm0,[rcx+8] / movss [rip+c],xmm0 / ret: a point copied into a global.
const uint8_t kPointSetter[] = {
    0xF3, 0x0F, 0x10, 0x01, 0xF3, 0x0F, 0x11, 0x05, 0, 0, 0, 0,
    0xF3, 0x0F, 0x10, 0x49, 0x04, 0xF3, 0x0F, 0x11, 0x0D, 0, 0, 0, 0,
    0xF3, 0x0F, 0x10, 0x41, 0x08, 0xF3, 0x0F, 0x11, 0x05, 0, 0, 0, 0, 0xC3
};
const char kPointSetterMask[] = "xxxxxxxx????xxxxxxxxx????xxxxxxxxx????x";

// How far before the setter's call its caller asks for the translation.
constexpr size_t kCallerWindow = 0x400;

bool IsCallTo(uintptr_t site, uintptr_t target) {
    return *reinterpret_cast<const uint8_t*>(site) == 0xE8 &&
           site + 5 + *reinterpret_cast<const int32_t*>(site + 1) == target;
}

// True when a call to `setter` anywhere in .text follows a call to `translation`
// within the window.
bool FedByStateTranslation(const TextSection& text, uintptr_t setter, uintptr_t translation) {
    static std::atomic<uint64_t> s_faults{0};
    __try {
        for (uintptr_t site = text.start; site + 5 <= text.start + text.size; ++site) {
            if (!IsCallTo(site, setter)) continue;
            const uintptr_t from = site - text.start > kCallerWindow ? site - kCallerWindow : text.start;
            for (uintptr_t before = from; before < site; ++before) {
                if (IsCallTo(before, translation)) return true;
            }
        }
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "first-person eye setter scan", s_faults)) {
    }
    return false;
}

}  // namespace

void SetFirstPersonEyeOffset(const NiPoint3& offset) {
    std::lock_guard<std::mutex> lock(g_offsetMutex);
    g_offset = offset;
}

bool InstallFirstPersonViewHook(const TextSection& text) {
    using cameraunlock::memory::ScanPatternMaskInRange;
    const uintptr_t translation =
        FindUniquePattern(text, kStateTranslation, kStateTranslationMask, "first-person state translation");
    if (translation == 0) {
        Log::Line("ERROR: first-person view: the first-person state's translation was not found - the weapon"
                  " keeps its place in the frame under a lean, and what is drawn at it does not");
        return false;
    }

    uintptr_t setter = 0;
    int fed = 0;
    uintptr_t cursor = text.start;
    const uintptr_t end = text.start + text.size;
    while (cursor < end) {
        const uintptr_t hit = reinterpret_cast<uintptr_t>(ScanPatternMaskInRange(
            cursor, end - cursor, kPointSetter, kPointSetterMask, sizeof(kPointSetter)));
        if (hit == 0) break;
        if (FedByStateTranslation(text, hit, translation)) {
            setter = hit;
            ++fed;
        }
        cursor = hit + 1;
    }
    if (fed != 1) {
        Log::Line("ERROR: first-person view: %d setters are fed by the first-person state's translation, not"
                  " one - the weapon keeps its place in the frame under a lean, and what is drawn at it does"
                  " not", fed);
        return false;
    }

    if (!g_setEyeHook.Install(reinterpret_cast<void*>(setter), reinterpret_cast<void*>(&SetEyeHook),
                              reinterpret_cast<void**>(&g_originalSetEye), "first-person eye setter")) {
        return false;
    }
    Log::Line("first-person view hook installed - the weapon is drawn from the leaned eye");
    return true;
}

void RemoveFirstPersonViewHook() {
    g_setEyeHook.Remove();
}

} // namespace Fallout4HT
