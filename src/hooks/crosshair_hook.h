// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

#include "module_scan.h"

namespace Fallout4HT {

// With the fire path decoupled, shots leave along body aim while the view
// follows the head, so a crosshair welded to screen centre points at nothing.
// These hooks move the native reticle to where body aim actually lands.
//
// Optional: without them shots still follow body aim, the reticle just stays at
// screen centre. Logs its own outcome.
void InstallCrosshairHook(const TextSection& text, uintptr_t moduleBase);

void RemoveCrosshairHook();

// The aim marker of free look with a marker. The HUD hides its crosshair while
// the sights are up (the clip's Visible goes off and its alpha stays at 100), so
// the marker is that crosshair, asked for at `opacity` and placed on `ndc`, where
// the round will land in the head-tracked view. An opacity of 0 hands the
// crosshair back to the HUD.
struct AimMarkerState {
    float ndcX;
    float ndcY;
    float opacity;
};

// Camera thread, once per camera tick.
void PublishAimMarker(const AimMarkerState& marker);

} // namespace Fallout4HT
