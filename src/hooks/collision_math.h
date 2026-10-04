// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Fallout4HT {
inline float CollisionTraceRange(float maxDistance, float margin) {
    return maxDistance + margin * 9.0f;
}

inline float CollisionHitAllowance(float distance, float margin, float normalDotDirection) {
    // LeanClamp subtracts margin once more after this normal-space correction.
    return distance - margin / std::max(0.1f, std::fabs(normalDotDirection)) + margin;
}

// Whether a body on this collision filter is level geometry, the only thing a
// lean stops for. The camera channel also collides with the game's own camera
// sphere (layer 36), which trails the third-person camera by a few units
// whenever the player moves, and with actors' bodies and power armour (layers
// 8, 30 and the data-defined ones), the player's own included: about 30 units
// from the first-person eye. A lean held off any of those is cut in open ground.
// Whether a ray is the head-tracked camera's own view axis: it starts at the
// tracked eye and runs along the tracked forward. `direction` and `forward` are
// unit vectors. The engine's pick for what the player can activate is that ray,
// built from the rendered camera, and it has to follow the aim instead.
inline bool IsViewAxisRay(const float* start, const float* direction, const float* trackedEye,
                          const float* trackedForward) {
    constexpr float kEyeToleranceUnits = 0.5f;
    // The cosine of 0.1 degrees.
    constexpr float kAxisCosine = 0.9999985f;
    const float dx = start[0] - trackedEye[0], dy = start[1] - trackedEye[1], dz = start[2] - trackedEye[2];
    if (dx * dx + dy * dy + dz * dz > kEyeToleranceUnits * kEyeToleranceUnits) return false;
    return direction[0] * trackedForward[0] + direction[1] * trackedForward[1] +
           direction[2] * trackedForward[2] >= kAxisCosine;
}

inline bool CollisionLayerBlocksLean(uint32_t filterInfo) {
    switch (filterInfo & 0x7F) {
    case 1:   // static
    case 2:   // animated static: doors
    case 3:   // transparent
    case 9:   // trees
    case 10:  // props
    case 13:  // terrain
    case 17:  // ground
    case 26:  // small transparent
    case 27:  // invisible wall
    case 28:  // small animated transparent
    case 29:  // large clutter
        return true;
    default:
        return false;
    }
}
}
