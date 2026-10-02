// SPDX-License-Identifier: MIT

#pragma once

#include "game/fallout4_types.h"

#include <cstdint>

namespace Fallout4HT::AdsLean {

// Whether the player has the sights up this frame, read off the game's own gun
// state every tick rather than latched from an event. An unreadable player reads
// "not aiming", the direction in which the lean comes back.
bool IsAiming(uintptr_t player);

// Whether the game is running its first-person camera, the one whose update reads
// its eye out of the first-person skeleton. False for an unreadable camera.
bool IsFirstPersonCamera(void* camera);

// Resolves what moving the skeleton needs from the game image. Without it there is
// no rig: FirstPersonRig returns 0.
void Install(HMODULE gameModule);

// The first-person skeleton root: the transform the first-person camera's eye,
// the arms, the held weapon and its projectile node all hang off. Only for the
// first-person camera: in any other the rig must stay where the game puts it.
uintptr_t FirstPersonRig(uintptr_t player);

// Carries `world` on the rig. Must run before the engine's camera update, which
// reads the eye from the skeleton: the camera, the weapon and the round then all
// move together. The engine rewrites the root from the player every frame, so the
// offset never accumulates across frames and releasing it is simply not writing it
// again. A second camera tick in one frame replaces the first tick's write.
void CarryOnRig(uintptr_t rig, const NiPoint3& world);

// Moves the skeleton's rendered geometry by `world` without moving the camera,
// for true free look: run after the camera update, it leaves the weapon where it
// is in the world while the eye moves. Returns false if the tree could not be
// walked.
bool ShiftWeapon(uintptr_t rig, const NiPoint3& world);

}  // namespace Fallout4HT::AdsLean
