// SPDX-License-Identifier: MIT

#pragma once

#include "config.h"

#include <cameraunlock/input/hotkey_poller.h>

namespace Fallout4HT {

// Nav-cluster hotkeys, their Ctrl+Shift chord aliases (AGENTS.md "Chord
// Alternatives"), and the diagnostics - Ctrl+Shift chords too, plus Insert - all
// polled on one core HotkeyPoller thread (~60Hz).
//
// No diagnostic may sit on an F-key: F5 is Fallout 4's quicksave and F9 its
// quickload, and two of these once did, so arming an instrument saved the game
// and running an A/B reloaded it.
class Hotkeys {
public:
    bool Start(const Config& cfg);
    void Stop();

private:
    cameraunlock::input::HotkeyPoller m_poller;
    bool m_started = false;
};

} // namespace Fallout4HT
