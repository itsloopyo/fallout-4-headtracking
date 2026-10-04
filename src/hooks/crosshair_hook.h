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

} // namespace Fallout4HT
