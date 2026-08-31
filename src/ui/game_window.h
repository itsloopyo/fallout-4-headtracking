// SPDX-License-Identifier: MIT

#pragma once

namespace Fallout4HT {

// Block until the game has a window, which is the only readiness signal this
// mod has that the engine is actually up. Returns false if it never appeared
// within the timeout. A fixed sleep was what this replaced, and it was tuned on
// machines where the window exists two seconds in; on a slower one the window
// was still absent at thirteen seconds and every one-shot lookup that followed
// missed, silently, for the whole session.
bool WaitForGameWindow(unsigned timeoutMillis);

// Centre the game window once, at startup, if it is in true windowed mode.
void CenterGameWindow();

// Never returns. Fallout 4 restores its window to the iLocation X/Y recorded in
// the prefs INI, which is commonly 0,0 - so a mode or resolution change
// mid-session parks the window in the top-left corner, a poor place to sit for
// head tracking. This keeps windowed mode centred for the life of the process.
void WatchWindowPlacement();

// Width divided by height of the game's client area, or 0 if the window cannot
// be found. This is the aspect the HUD is laid out against, and it is NOT
// always the aspect of the engine's own camera frustum: builds before the
// 2024-04-25 update have no 21:9 or 32:9 support, so they render a narrower
// frustum stretched across the whole screen. Reading the window is what makes
// the reticle land at any resolution rather than only where the two agree.
double GetViewportAspect();

} // namespace Fallout4HT
