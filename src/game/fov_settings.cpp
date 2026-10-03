// SPDX-License-Identifier: MIT

#include "pch.h"

#include "game/fov_settings.h"
#include "core/logging.h"
#include "core/seh_guard.h"
#include "game/fallout4_types.h"

#include <cameraunlock/camera/zoom_compensation.h>

#include <atomic>
#include <cmath>

namespace Fallout4HT {
namespace FovSettings {
namespace {

// About half a second at 60 fps of the members accounting for the frustum, so a
// chance agreement on a build that keeps something else there proves nothing.
constexpr int kTicksToProve = 30;

// The line is for a person to check, not a trace of the zoom animation.
constexpr uint64_t kMinReportIntervalMs = 250;

// Camera-thread only.
struct ModeProof {
    int agreeingTicks;
    bool proven;
};
ModeProof g_firstPersonProof{};
ModeProof g_worldProof{};

std::atomic<float> g_zoomFactor{1.0f};

bool ReadCameraFov(void* playerCamera, bool firstPerson, CameraFov& out) {
    static std::atomic<uint64_t> s_faults{0};
    __try {
        const uintptr_t camera = reinterpret_cast<uintptr_t>(playerCamera);
        out.mode = *reinterpret_cast<const float*>(
            camera + (firstPerson ? PlayerCameraOffsets::FirstPersonFov : PlayerCameraOffsets::WorldFov));
        out.zoomAdjust = *reinterpret_cast<const float*>(camera + PlayerCameraOffsets::FovAdjustCurrent);
        out.animatorAdjust = *reinterpret_cast<const float*>(camera + PlayerCameraOffsets::FovAnimatorAdjust);
        return std::isfinite(out.mode) && std::isfinite(out.zoomAdjust) && std::isfinite(out.animatorAdjust);
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "camera fov read", s_faults)) {
    }
    return false;
}

} // namespace

void NoteRenderedFrustum(void* playerCamera, bool firstPerson, float frustumRight, float frustumTop) {
    if (!(frustumRight > 0.0f) || !(frustumTop > 0.0f)) return;

    CameraFov fov{};
    const bool read = ReadCameraFov(playerCamera, firstPerson, fov);
    ModeProof& proof = firstPerson ? g_firstPersonProof : g_worldProof;
    const bool wasProven = proof.proven;
    if (read && FovAccountsForFrustum(fov, frustumTop)) {
        if (++proof.agreeingTicks >= kTicksToProve) proof.proven = true;
    } else {
        proof.agreeingTicks = 0;
    }

    // The reference is what the engine would render with its zoom off, which
    // carries the animator's adjust: with it left out, a first-person idle that
    // narrows the view by two degrees would read as a zoom of 0.964 for the
    // whole of ordinary play.
    const float unzoomed = read ? UnzoomedTangent(fov) : 0.0f;
    const float factor = proof.proven && unzoomed > 0.0f
        ? cameraunlock::camera::FovZoomFactor(frustumTop, unzoomed) : 1.0f;
    g_zoomFactor.store(factor, std::memory_order_relaxed);

    static bool s_reported = false;
    static float s_lastTop = 0.0f;
    static uint64_t s_lastReportMs = 0;
    const uint64_t nowMs = GetTickCount64();
    // 0.5% - below what a zoom does and above float noise on a steady frustum.
    const bool moved = s_lastTop > 0.0f && std::fabs(frustumTop - s_lastTop) > s_lastTop * 0.005f;
    const bool proved = proof.proven && !wasProven;
    if (s_reported && !proved && !(moved && nowMs - s_lastReportMs >= kMinReportIntervalMs)) return;
    s_reported = true;
    s_lastTop = frustumTop;
    s_lastReportMs = nowMs;

    constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;
    Log::Line("FOV basis: rendered h=%.2f deg v=%.2f deg (tan half v=%.5f, display aspect %.4f)"
              " | %s camera fov %.2f, zoom adjust %.3f, animator adjust %.3f | un-zoomed tan half v=%.5f"
              " | %s | zoom factor %.4f",
              2.0f * std::atan(frustumRight) * kRadToDeg, 2.0f * std::atan(frustumTop) * kRadToDeg, frustumTop,
              frustumRight / frustumTop, firstPerson ? "first-person" : "world", fov.mode, fov.zoomAdjust,
              fov.animatorAdjust, unzoomed,
              !read ? "the camera's fov could not be read, so no zoom compensation"
              : proof.proven ? "the camera's fov accounts for the rendered frustum"
                             : "the camera's fov has not yet accounted for the rendered frustum, so no zoom"
                               " compensation",
              factor);
}

float CurrentZoomFactor() {
    return g_zoomFactor.load(std::memory_order_relaxed);
}

} // namespace FovSettings
} // namespace Fallout4HT
