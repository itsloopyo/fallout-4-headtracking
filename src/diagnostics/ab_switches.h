// SPDX-License-Identifier: MIT

#pragma once

namespace Fallout4HT {

namespace AbSwitches {

// Ctrl+Shift+K: move the native reticle to the body-aim point. Off leaves it at
// screen centre, which is the only other thing this mod puts there - so it is
// the way to tell a reticle artifact from a camera one.
bool CrosshairMoveEnabled();
void ToggleCrosshairMove();

// Ctrl+Shift+N: take the head pose off niCamera for the length of a clean
// gameplay scope, the way this mod used to. OFF by default because it was the
// flicker: the render thread samples the camera without taking our lock, so it
// catches that window on about a tenth of frames and lights them from the
// body-aimed camera. Kept as a switch so the A/B can be re-run in place rather
// than across sessions, which is the only comparison that has ever meant
// anything here.
bool StripPoseInCleanScope();
void ToggleStripPoseInCleanScope();

} // namespace AbSwitches
} // namespace Fallout4HT
