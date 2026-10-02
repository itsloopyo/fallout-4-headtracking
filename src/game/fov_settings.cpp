// SPDX-License-Identifier: MIT

#include "pch.h"

#include "game/fov_settings.h"
#include "core/logging.h"
#include "core/seh_guard.h"
#include "hooks/module_scan.h"
#include "game/fallout4_types.h"
#include "game/game_setting.h"

#include <cameraunlock/camera/zoom_compensation.h>

#include <atomic>
#include <cmath>
#include <cstring>

namespace Fallout4HT {
namespace FovSettings {
namespace {

// A field of view the engine would actually render. Used to reject a .data slot
// that happens to hold the same pointer for some other reason.
constexpr float kMinPlausibleFov = 1.0f;
constexpr float kMaxPlausibleFov = 179.0f;

const float* g_firstPersonFov = nullptr;
const float* g_worldFov = nullptr;

// 0.5%: far inside any zoom the game applies, far outside float noise on a
// frustum the engine is holding still.
constexpr float kBaseMatchTolerance = 0.005f;

// About half a second at 60 fps. Long enough that a zoom animation sweeping
// past the other setting's value cannot latch it.
constexpr int kTicksToLatchBase = 30;

std::atomic<float> g_baseTangent{0.0f};
std::atomic<float> g_liveTangent{0.0f};

// Camera-thread only, so plain floats.
float g_pendingBase = 0.0f;
int g_pendingTicks = 0;

bool ReadFloat(const float* at, float& out) {
    static std::atomic<uint64_t> s_faults{0};
    __try {
        out = *at;
        return true;
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "fov setting read", s_faults)) {
    }
    return false;
}

// Every .data slot pointing at the name is a candidate Setting::name. Exactly
// one must survive the plausibility check: two would mean this is not the
// object layout assumed here, and reading on would hand the compensation a
// reference that is not a field of view at all.
const float* FindSettingValue(uintptr_t moduleBase, const char* name) {
    const char* nameString = FindSettingName(moduleBase, name);
    if (nameString == nullptr) {
        Log::Line("WARN: FOV setting '%s' has no name string in this build", name);
        return nullptr;
    }

    uintptr_t dataStart = 0;
    size_t dataSize = 0;
    if (!FindSection(moduleBase, ".data", dataStart, dataSize)) return nullptr;

    const uintptr_t wanted = reinterpret_cast<uintptr_t>(nameString);
    const float* found = nullptr;
    size_t hits = 0;
    static std::atomic<uint64_t> s_faults{0};
    __try {
        for (size_t off = 0; off + sizeof(uintptr_t) <= dataSize; off += 8) {
            if (*reinterpret_cast<const uintptr_t*>(dataStart + off) != wanted) continue;
            const uintptr_t setting = dataStart + off - kSettingNameOffset;
            const float* value = reinterpret_cast<const float*>(setting + kSettingValueOffset);
            const float v = *value;
            if (!(v >= kMinPlausibleFov && v <= kMaxPlausibleFov)) continue;
            found = value;
            ++hits;
        }
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "fov setting scan", s_faults)) {
    }

    if (hits != 1) {
        Log::Line("WARN: FOV setting '%s' matched %zu plausible objects - not using it", name, hits);
        return nullptr;
    }
    return found;
}

bool Read(const float* at, float& out) {
    if (at == nullptr) return false;
    float v = 0.0f;
    if (!ReadFloat(at, v)) return false;
    if (!(v >= kMinPlausibleFov && v <= kMaxPlausibleFov)) return false;
    out = v;
    return true;
}

// The tan(vfov/2) the engine renders a setting at, un-zoomed. 0 if unreadable.
float BaseTangentOf(const float* setting) {
    float degrees = 0.0f;
    if (!Read(setting, degrees)) return 0.0f;
    return BaseTangentForFovDegrees(degrees);
}

} // namespace

bool Initialize() {
    const uintptr_t moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(GAME_EXE));
    if (moduleBase == 0) return false;

    g_firstPersonFov = FindSettingValue(moduleBase, "fDefault1stPersonFOV:Display");
    g_worldFov = FindSettingValue(moduleBase, "fDefaultWorldFOV:Display");

    float first = 0.0f;
    float world = 0.0f;
    const bool haveFirst = Read(g_firstPersonFov, first);
    const bool haveWorld = Read(g_worldFov, world);
    if (haveFirst || haveWorld) {
        Log::Line("FOV settings: fDefault1stPersonFOV=%.2f (%s) fDefaultWorldFOV=%.2f (%s)"
                  " - read live, so the console's fov command is followed without a restart",
                  first, haveFirst ? "ok" : "unreadable",
                  world, haveWorld ? "ok" : "unreadable");
        return true;
    }
    Log::Line("WARN: neither FOV setting could be read - zoom compensation stays off");
    return false;
}

bool TryGetFirstPersonFovDegrees(float& out) { return Read(g_firstPersonFov, out); }
bool TryGetWorldFovDegrees(float& out) { return Read(g_worldFov, out); }

void NoteRenderedFrustum(float frustumRight, float frustumTop) {
    if (!(frustumRight > 0.0f) || !(frustumTop > 0.0f)) return;

    // Which of the two settings is the reference depends on the camera mode, and
    // the mode is not cheaply readable. It does not have to be: un-zoomed, the
    // engine renders at one of them EXACTLY, so the mode identifies itself. The
    // base is latched when the rendered frustum sits on a candidate and stays
    // there, and held while a zoom takes it away.
    //
    // This is not a maximum tracked at runtime, which the doctrine forbids and
    // for good reason - one wide cinematic would poison it for the session.
    // Nothing is latched that does not match a setting the game is holding now,
    // and a frustum matching neither leaves the last reference alone.
    //
    // The gap this leaves, stated rather than hidden: a zoom that comes to REST
    // within half a percent of the other mode's configured FOV, and stays there
    // past the latch, is read as that mode rather than as a zoom. The
    // compensation then reverts to 1.0 for as long as it lasts, which is the
    // behaviour the mod had before any of this, so the failure is a lost
    // correction and never a wrong one.
    const float candidates[] = { BaseTangentOf(g_firstPersonFov), BaseTangentOf(g_worldFov) };
    bool onCandidate = false;
    for (float candidate : candidates) {
        if (candidate <= 0.0f) continue;
        if (std::fabs(frustumTop - candidate) > candidate * kBaseMatchTolerance) continue;
        onCandidate = true;
        if (g_pendingBase > 0.0f &&
            std::fabs(candidate - g_pendingBase) <= g_pendingBase * kBaseMatchTolerance) {
            ++g_pendingTicks;
        } else {
            g_pendingBase = candidate;
            g_pendingTicks = 1;
        }
        // Held rather than passed through. A zoom animation sweeps across the
        // other setting's value on its way, and latching that would leave the
        // factor referenced to a FOV the camera was never resting at.
        if (g_pendingTicks >= kTicksToLatchBase) {
            g_baseTangent.store(candidate, std::memory_order_relaxed);
        }
        break;
    }
    if (!onCandidate) {
        g_pendingBase = 0.0f;
        g_pendingTicks = 0;
    }

    g_liveTangent.store(frustumTop, std::memory_order_relaxed);

    static std::atomic<bool> s_reported{false};
    static float s_lastTop = 0.0f;
    static float s_lastBase = 0.0f;
    const float latched = g_baseTangent.load(std::memory_order_relaxed);
    const bool first = !s_reported.exchange(true);
    // 0.5% - below what a zoom does and above float noise on a steady frustum.
    const bool moved = s_lastTop > 0.0f &&
                       std::fabs(frustumTop - s_lastTop) > s_lastTop * 0.005f;
    // Reporting the frustum alone hides the steady state: the line fires at the
    // instant the FOV moves, which is before the base has re-latched, so it
    // shows a factor that is about to stop being true. The settled reading is
    // the one the gate is read from, and it only exists once the base lands.
    const bool rebased = std::fabs(latched - s_lastBase) > 1e-6f;
    if (!first && !moved && !rebased) return;
    s_lastTop = frustumTop;
    s_lastBase = latched;

    constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;
    const float hfov = 2.0f * std::atan(frustumRight) * kRadToDeg;
    const float vfov = 2.0f * std::atan(frustumTop) * kRadToDeg;

    float firstPerson = 0.0f;
    float world = 0.0f;
    const bool haveFirst = Read(g_firstPersonFov, firstPerson);
    const bool haveWorld = Read(g_worldFov, world);
    Log::Line("FOV basis: rendered h=%.2f deg v=%.2f deg (tan half v=%.5f, display aspect %.4f)"
              " | settings 1stPerson=%.2f world=%.2f | base tan half v=%.5f | zoom factor %.4f",
              hfov, vfov, frustumTop, frustumRight / frustumTop,
              haveFirst ? firstPerson : -1.0f, haveWorld ? world : -1.0f,
              latched, CurrentZoomFactor());
}

float CurrentZoomFactor() {
    const float base = g_baseTangent.load(std::memory_order_relaxed);
    const float live = g_liveTangent.load(std::memory_order_relaxed);
    if (!(base > 0.0f) || !(live > 0.0f)) return 1.0f;
    return cameraunlock::camera::FovZoomFactor(live, base);
}

} // namespace FovSettings
} // namespace Fallout4HT
