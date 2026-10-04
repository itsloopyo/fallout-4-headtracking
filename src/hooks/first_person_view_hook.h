// SPDX-License-Identifier: MIT

#pragma once

#include "game/fallout4_types.h"
#include "module_scan.h"

namespace Fallout4HT {

// The arms and the weapon are drawn from the eye the frame is drawn from.
//
// The engine draws them in a pass of its own. Its camera takes the world
// camera's rotation, so a turned head already shows on the weapon, but its
// position is the first-person state's own eye, handed to the renderer once a
// frame through a setter. The lean this mod puts on the world camera never
// reached it: the world moved under a lean and the weapon kept its place in the
// frame, so everything the world pass draws at the weapon (muzzle flash, beams,
// tracers, casings) came out beside it. The hook adds the camera's lean to the
// eye that setter is given, and nothing the game itself reads changes.
//
// The setter is found by shape and proved by its caller, which must call the
// first-person state's translation just before it. False when that does not
// single out one function.
bool InstallFirstPersonViewHook(const TextSection& text);
void RemoveFirstPersonViewHook();

// What the camera adds to the first-person eye this frame, in world units, which
// are also the first-person space's: that space is the world moved to the player.
void SetFirstPersonEyeOffset(const NiPoint3& offset);

} // namespace Fallout4HT
