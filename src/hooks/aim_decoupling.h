// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

#include "module_scan.h"

namespace Fallout4HT {

// Head tracking moves the view but must not move the shot.
//
// Two things steer a shot and they need opposite treatment, which is why these
// two hooks do NOT do the same thing to their targets:
//
//   The fire path is sandwiched. It is shown the CLEAN camera rotation for the
//     length of one call, so the engine computes the angles the mouse asked for
//     with no angle maths on our side and nothing else in the frame disturbed.
//
//   The auto-aim solver is NOT sandwiched. It runs every frame, and leaving
//     niCamera body-aimed for the length of each solve put ~1% of view builds
//     back on the un-tracked camera - render-thread exposure of exactly the kind
//     the flicker is made of. Its hook only records the arguments the game last
//     used, so the fire path can re-run the solve against the body's camera once
//     per shot, where the window costs nothing.

// Sandwiches the projectile launch builder, and re-solves auto-aim inside that
// window. MANDATORY: without it, shots follow the head, so the caller must
// refuse to run head tracking at all if this fails. Returns false if the
// function could not be located or the hook not created.
bool InstallFirePathHook(const TextSection& text, uintptr_t moduleBase);

// Captures the auto-aim solver's arguments so the fire path can replay them.
// Mandatory for the same reason as the fire-path hook: without the replay, the
// launch converges onto a cached aim point that was solved from the head-tracked
// camera, and a 35 degree head turn swings the shot 46.5 degrees even with a
// clean camera under it.
bool InstallAutoAimHook(const TextSection& text, uintptr_t moduleBase);

void RemoveAimDecouplingHooks();

} // namespace Fallout4HT
