// SPDX-License-Identifier: MIT

#pragma once

namespace Fallout4HT {

// Centre the game window once, at startup, if it is in true windowed mode.
void CenterGameWindow();

// Never returns. Fallout 4 restores its window to the iLocation X/Y recorded in
// the prefs INI, which is commonly 0,0 - so a mode or resolution change
// mid-session parks the window in the top-left corner, a poor place to sit for
// head tracking. This keeps windowed mode centred for the life of the process.
void WatchWindowPlacement();

} // namespace Fallout4HT
