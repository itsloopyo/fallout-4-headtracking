// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

namespace Fallout4HT {

// Rolling record of what the mod feeds the camera, one row per tick, dumped on
// demand.
//
// It records continuously rather than being armed for a window, because the
// person who can see the fault cannot press a key BEFORE it happens. They press
// it after, and the buffer already holds the seconds leading up to it.
//
// The per-second audit summarises, and every summary of this bug so far has read
// clean. Only per-tick values separate the three things that can put an
// un-tracked frame on screen: the tracker sending one (raw), the pipeline
// producing one (interpolated / processed), or the camera path losing it
// (applied). Averages hide all three.
void DumpPoseTrace();

// Called once per camera tick with what this frame will actually use, and with
// the pipeline's intermediate values so the stage that loses the motion is
// visible: what arrived from the tracker, what the interpolator produced, what
// came out the far end, and what the camera was actually rotated by.
struct PoseTickRecord {
    float rawYaw;
    float rawPitch;
    float interpolatedYaw;
    float processedYaw;
    float processedPitch;
    float deltaTime;
    bool newSample;
    float cameraSwingDeg;
    // Angle between the clean and head-tracked camera this tick, or
    // kNothingApplied when the tick returned without touching the camera at all.
    //
    // The distinction has to be explicit. A near-zero angle happens for two
    // completely different reasons - the head is centred, or the mod applied
    // nothing and the frame rendered from the engine's own camera - and reading
    // "small angle" as "un-tracked frame" turns every centred moment into a false
    // positive. That misreading cost a whole analysis pass.
    float appliedDeg;
    // Cumulative receiver counters. A step in either between consecutive rows
    // names the reason a tick had nothing new: a second tracker app (rejected)
    // or a tracker that stopped tracking and repeated a pose (frozen).
    uint64_t rejectedPackets;
    uint64_t frozenPackets;
};

// Sentinel for appliedDeg: this tick rendered from the engine's own camera.
constexpr float kNothingApplied = -1.0f;

void RecordPoseTick(const PoseTickRecord& record);

} // namespace Fallout4HT
