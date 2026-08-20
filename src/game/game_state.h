// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

namespace Fallout4HT {

// Gates head tracking to actual gameplay, which for Fallout 4 means keeping it
// off for both halves of VATS. Head tracking is actively wrong in each, for its
// own reason:
//
//  - The targeting menu does not reframe. It freezes the view you had and labels
//    the target through it, so a head-turned camera puts the target off screen
//    and the body-part percentages somewhere else again. The pose has to come
//    off BEFORE the freeze: the widgets project through a view matrix cached on
//    the game thread, and that thread is what stops.
//  - The attack sequence is a shot the game has framed for itself. Steering that
//    camera with the head is the same intrusion as steering a cutscene.
//
// The two need different signals because they are different things to the
// engine. The attack sequence swaps PlayerCamera's current state for the VATS
// one. The targeting menu changes no camera state at all, so it is read as a
// mode byte inside the VATS singleton, which RTTI locates. Both are resolved at
// runtime, so a game patch moves them for free and neither is pinned per build.
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

// The VATS singleton's address, or 0 if it was not resolved. The flag search in
// vats_probe.cpp reports its candidates as offsets into this object, which is
// how the mode byte was found and how it would be found again after a patch.
uintptr_t VatsSingletonAddress();

} // namespace Fallout4HT
