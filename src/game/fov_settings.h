// SPDX-License-Identifier: MIT

#pragma once

#include <cmath>

namespace Fallout4HT {

// What tan(vfov/2) the engine renders a field of view of `degrees` at.
//
// The engine's FOV angles are the HORIZONTAL field of view at a fixed 16:9
// reference, not at the display's own aspect and not a vertical angle. Measured
// against a running game on a 3.5556 display rather than assumed, because
// pairing a vertical live value with a horizontal reference is a silent constant
// multiplier on the whole of normal play:
//
//   80 degrees  -> tan(40 deg) / (16/9) = 0.47200, rendered frustumTop 0.47199
//   100 degrees -> tan(50 deg) / (16/9) = 0.67036, rendered frustumTop 0.67036
//
// so the engine holds the vertical implied by the 16:9 reference and widens
// horizontally for anything wider. Both sides of the zoom ratio are therefore
// tan(vfov/2), which is the whole of the units question.
inline float BaseTangentForFovDegrees(float degrees) {
    constexpr float kReferenceAspect = 16.0f / 9.0f;
    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
    if (!(degrees > 0.0f) || !(degrees < 180.0f)) return 0.0f;
    return std::tan(degrees * 0.5f * kDegToRad) / kReferenceAspect;
}

// PlayerCamera's own field of view, in degrees. The engine renders the mode's
// angle plus both adjusts: measured in first person, 80 with an animator adjust
// of -2.061 rendered 77.94 at the hip, and 80 with a zoom adjust of -21.555
// rendered 58.45 through a reflex sight, where the animator adjust was 0.
struct CameraFov {
    // The angle the player set for the camera mode in use: the first-person one
    // in first person, the world one in every other.
    float mode;
    // The game's zoom: 0 at the hip, the weapon's own zoom with the sights up.
    float zoomAdjust;
    // What the running animation adds. Part of the un-zoomed view: it is there
    // at the hip and it is not something a head movement should be scaled for.
    float animatorAdjust;
};

inline float RenderedTangent(const CameraFov& fov) {
    return BaseTangentForFovDegrees(fov.mode + fov.zoomAdjust + fov.animatorAdjust);
}

// What the engine would be rendering with no zoom on: the reference a zoom is
// measured against.
inline float UnzoomedTangent(const CameraFov& fov) {
    return BaseTangentForFovDegrees(fov.mode + fov.animatorAdjust);
}

// Whether the members account for the frustum the engine actually rendered,
// within half a percent. This is what says the members are where they are read
// from on the running build, and that this camera mode renders from them.
inline bool FovAccountsForFrustum(const CameraFov& fov, float frustumTop) {
    const float expected = RenderedTangent(fov);
    return expected > 0.0f && std::fabs(frustumTop - expected) <= expected * 0.005f;
}

namespace FovSettings {

// Feeds one camera tick in: the PlayerCamera, whether it is running its
// first-person state, and the rendered frustum normalised to near = 1
// (right = tan(hfov/2), top = tan(vfov/2)). Logs the terms once, then whenever
// the rendered FOV moves.
//
// A factor that is wrong by a constant reads exactly like a factor that is
// right, so the terms go on a line a human can check, and the gate is that it
// says 1.0000 in ordinary gameplay.
void NoteRenderedFrustum(void* playerCamera, bool firstPerson, float frustumRight, float frustumTop);

// What yaw, pitch and the lean across the view scale by so their effect on
// screen is the size it would have been un-zoomed. Exactly 1.0 with no zoom on,
// and 1.0 until the camera's FOV members have been seen to account for the
// rendered frustum in the camera mode in use - never a guess. Roll and the lean
// along the view do not take it.
float CurrentZoomFactor();

} // namespace FovSettings
} // namespace Fallout4HT
