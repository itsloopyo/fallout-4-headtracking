// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cmath>

namespace Fallout4HT {
inline float CollisionTraceRange(float maxDistance, float margin) {
    return maxDistance + margin * 9.0f;
}

inline float CollisionHitAllowance(float distance, float margin, float normalDotDirection) {
    // LeanClamp subtracts margin once more after this normal-space correction.
    return distance - margin / std::max(0.1f, std::fabs(normalDotDirection)) + margin;
}
}
