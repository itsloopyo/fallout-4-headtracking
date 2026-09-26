// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

namespace Fallout4HT {

// Version info
inline constexpr const char* VERSION = "0.0.0";

// Target game executable
inline constexpr const char* GAME_EXE = "Fallout4.exe";

// Shared math constants.
//
// These are NOT reciprocals of each other and must not be treated as such. Every
// site that converts radians to degrees does so with RAD_TO_DEG; the handful
// that divide by DEG_TO_RAD instead land about 4e-7 relative lower, and were
// left that way deliberately when the conversions were named, because changing
// them would move printed diagnostics in their last digit for no gain.
inline constexpr float DEG_TO_RAD = 0.0174533f;
inline constexpr float RAD_TO_DEG = 57.2957795f;

} // namespace Fallout4HT
