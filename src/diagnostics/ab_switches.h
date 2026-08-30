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

// Ctrl+Shift+M: sweep the reticle between stage offsets of -640 and +640 in a
// slow loop, ignoring the aim entirely. This measures the one thing about the
// HUD that cannot be derived from the window: whether the movie's horizontal
// scale is capped at H/720 (half-stage 360*aspect, so 640 lands about halfway
// out) or stretches as W/1280 (half-stage a flat 640, so 640 lands exactly on
// the screen edge). Where the sweep stops answers it at a glance, at whatever
// resolution the player actually runs.
bool StageRulerEnabled();
void ToggleStageRuler();

} // namespace AbSwitches
} // namespace Fallout4HT
