// SPDX-License-Identifier: MIT

#pragma once

#include "game/fallout4_types.h"
#include "module_scan.h"

namespace Fallout4HT {

// The camera the game aims with and the one the frame is drawn from, as the pose
// now held on the camera left them.
struct AimReference {
    NiPoint3 cleanEye;
    NiPoint3 cleanForward;
    NiPoint3 trackedEye;
    NiPoint3 trackedForward;
};

// Set after each hold, cleared when a tick holds nothing.
void SetAimReference(const AimReference& reference);
void ClearAimReference();
bool GetAimReference(AimReference& out);

// What the player can activate follows the aim.
//
// The engine finds it by casting from the rendered camera: its caller copies the
// world camera's translation and forward row and hands both to the caster. With
// a head pose on that camera the cast finds what is at the centre of the view
// while the crosshair sits on the body's aim, so "E) TAKE" answers for something
// the crosshair is not on. The hook hands the caster the clean eye and the clean
// forward instead, and only when the pair it was given IS the tracked camera.
//
// The caster is found as the one function that reads both fActivatePickLength
// and fActivatePickRadius, each located by its setting name, so nothing here is
// an address. False when it is not found exactly once: activation then keeps
// following the view, and the log says so.
bool InstallActivationHook(uintptr_t moduleBase, const TextSection& text);
void RemoveActivationHook();

} // namespace Fallout4HT
