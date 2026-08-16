// SPDX-License-Identifier: MIT

#pragma once

#include <cmath>

#include "constants.h"

namespace Fallout4HT {

// Three-component primitives shared by the camera hooks and the instruments that
// watch them. They were open-coded in six files, and the two that matter are
// easy to get subtly wrong in ways that only show up at the extremes: an acos
// whose argument is not clamped returns NaN the moment a dot product rounds past
// 1.0, and every one of these dots is between vectors that are meant to be unit
// length, so that happens routinely.
//
// Header-only and inline on purpose: they run per view-matrix build, roughly two
// thousand times a second, and the call sites were previously inlined by virtue
// of being file-local.
//
// The operand order is the same left-to-right accumulation the open-coded
// versions used, so results are bit-identical to what they replaced.

// Dot product of two three-component arrays.
inline float Dot3(const float* a, const float* b) {
    float dot = 0.0f;
    for (int i = 0; i < 3; ++i) dot += a[i] * b[i];
    return dot;
}

// Pull a value back inside [-1, 1] before it is handed to acosf or asinf.
// Rounding routinely pushes the dot of two unit vectors, or a component of a
// supposedly-normalised row, a few ULP past 1.0, and both functions return NaN
// for that - which then propagates silently into a comparison that is false
// either way, so a guard reads as "nothing wrong" rather than failing.
inline float ClampToUnitRange(float value) {
    return value > 1.0f ? 1.0f : (value < -1.0f ? -1.0f : value);
}

// Angle between two UNIT vectors, in radians. Callers that are not already
// normalised must normalise first - this does not divide by the lengths.
inline float RadiansBetweenUnit(const float* a, const float* b) {
    return acosf(ClampToUnitRange(Dot3(a, b)));
}

// The same angle in degrees.
inline float DegreesBetweenUnit(const float* a, const float* b) {
    return RadiansBetweenUnit(a, b) * RAD_TO_DEG;
}

// Euclidean distance between two three-component arrays.
inline float Distance3(const float* a, const float* b) {
    float squared = 0.0f;
    for (int i = 0; i < 3; ++i) {
        const float d = a[i] - b[i];
        squared += d * d;
    }
    return sqrtf(squared);
}

} // namespace Fallout4HT
