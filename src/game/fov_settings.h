// SPDX-License-Identifier: MIT

#pragma once

#include <cmath>

namespace Fallout4HT {

// What tan(vfov/2) the engine renders an fDefault*FOV setting at, un-zoomed.
//
// The setting is the HORIZONTAL field of view at a fixed 16:9 reference, not at
// the display's own aspect and not a vertical angle. Measured against a running
// game on a 3.5556 display rather than assumed, because pairing a vertical live
// value with a horizontal reference is a silent constant multiplier on the whole
// of normal play:
//
//   setting  80 -> tan(40 deg) / (16/9) = 0.47200, rendered frustumTop 0.47199
//   setting 100 -> tan(50 deg) / (16/9) = 0.67036, rendered frustumTop 0.67036
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


// The game's un-zoomed field of view, read live out of the engine's own setting
// objects so the console's `fov` command is followed without a restart.
//
// Two settings, because Fallout 4 renders the two camera modes at different
// angles and ships them different by default (fDefault1stPersonFOV 80,
// fDefaultWorldFOV 70). Using one for both is a fixed ~20% error in whichever
// mode got the wrong one, and the only symptom is head tracking feeling weak.
namespace FovSettings {

// Locates both settings. Scans .data, so it belongs on the init thread rather
// than a camera tick. Returns false if neither could be found, which leaves
// zoom compensation off rather than guessing a reference.
bool Initialize();

// Live reads. False means the setting was not located or currently holds a
// value outside the range a field of view can take.
bool TryGetFirstPersonFovDegrees(float& out);
bool TryGetWorldFovDegrees(float& out);

// Feeds the rendered frustum in, once per camera tick, with the extents already
// read from the NiCamera and normalised to near = 1 (right = tan(hfov/2),
// top = tan(vfov/2)). Tracks which setting the engine is currently un-zoomed
// against and logs the basis - once, then whenever the rendered FOV moves.
//
// A factor that is wrong by a constant reads exactly like a factor that is
// right, so the terms go on a line a human can check, and the gate is that it
// says 1.0000 in ordinary gameplay.
void NoteRenderedFrustum(float frustumRight, float frustumTop);

// What yaw, pitch and the lean scale by so their effect on screen is the size
// it would have been un-zoomed. Exactly 1.0 in ordinary play, and 1.0 whenever
// the reference cannot be established - never a guess. Roll does not take it.
float CurrentZoomFactor();

} // namespace FovSettings
} // namespace Fallout4HT
