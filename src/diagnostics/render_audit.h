// SPDX-License-Identifier: MIT

#pragma once

namespace Fallout4HT {

// Arms the audit below for a few seconds, and starts a sampler thread that reads
// the camera matrices far faster than the frame rate. Bound to the matrix-dump
// hotkey. The per-tick audit can only see the camera at one fixed point in the
// frame; anything that swaps a matrix and swaps it back WITHIN a frame is
// invisible to it and plain in the sampler.
void ArmRenderAudit();

// Called at the TOP of the camera hook, before the engine's own update runs.
// At that instant cameraRoot still holds whatever the last writer left there and
// worldToCam still holds the matrix the frame just drawn was rendered with, so
// this is the one place that can answer whether the head-tracked rotation
// reached the screen or something overwrote it first. Does nothing unless armed.
void AuditRenderedFrame(void* thisCamera);

} // namespace Fallout4HT
