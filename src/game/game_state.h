// SPDX-License-Identifier: MIT

#pragma once

namespace Fallout4HT {

// Gates head tracking to actual gameplay, which for Fallout 4 means keeping it
// off for both halves of VATS. Head tracking is actively wrong in each, for its
// own reason:
//
//  - The targeting menu does not reframe. It freezes the view you had and labels
//    the target through it, so a head-turned camera puts the target off screen
//    and the body-part percentages somewhere else again.
//  - The attack sequence is a shot the game has framed for itself. Steering that
//    camera with the head is the same intrusion as steering a cutscene.
//
// The two need different signals because they are different things to the
// engine. The attack sequence swaps PlayerCamera's current state for the VATS
// one, which RTTI finds at runtime and a patch therefore moves for free. The
// targeting menu changes no camera state at all, so it is read from a byte in
// .data, pinned per build and gated on the PE fingerprint - it has no RTTI, no
// vtable and no code signature to find it by. An unrecognised build leaves that
// half of the gate off rather than reading a stale address; the attack-camera
// half keeps working regardless.
//
// Menus and loading screens are NOT gated here: they stop the game updating, and
// the pause watchdog in player_hook.cpp already takes the head pose off the
// camera whenever that happens.
class GameState {
public:
    static bool Initialize();
    // playerCamera is the PlayerCamera the engine is ticking; its current state
    // is what identifies the attack camera.
    static bool IsInGameplay(void* playerCamera);
};

} // namespace Fallout4HT
