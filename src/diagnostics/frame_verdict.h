// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

namespace Fallout4HT {

struct CameraRootSnapshots;
struct OverrideReference;
enum class OverridePath : uint8_t;

// Was the frame the renderer just built actually head-tracked?
//
// Every counter that came before this one compared the built matrix against the
// LIVE camera node, which is a self-consistency check: when the node is clean the
// build produces a clean matrix and the two agree, so a completely un-tracked
// frame scores as correct. That blind spot is why "0 LEFT STOCK" could be true
// while the view was visibly flicking. This instrument compares against the two
// states the camera could be in - the body-aimed one and the head-tracked one -
// and says which one the renderer got, separately for orientation and position.
void RecordBuildVerdict(const CameraRootSnapshots& snap, OverridePath path,
                        const OverrideReference& reference);

// The same question asked of the matrix the build produced, which is one step
// closer to the renderer than the node it read.
void RecordBuiltVerdict(const CameraRootSnapshots& snap,
                        const OverrideReference& reference);

// What the camera tick published, so a fault upstream of the camera is not
// mistaken for one in it. A tick that publishes no lean while still publishing a
// rotation moves the camera without turning it - which is the shape of the
// reported fault (same aim, body somewhere else on screen) and is invisible to
// every check on the camera itself, because the camera faithfully renders the
// pose it was given.
void RecordTickPose(bool haveRotation, bool hasPosition,
                    float leanX, float leanY, float leanZ, float appliedDeg);

// How many camera ticks the mod has seen, and how long ago the last one was.
//
// This is the one honest answer to "is the game actually running right now".
// VATS and every menu stop the camera ticking while the renderer keeps drawing,
// so the picture on screen keeps looking alive; a capture taken then reads as a
// flawless result, and a keystroke sent then means something completely
// different from what was intended.
void GetCameraTickLiveness(unsigned long long& totalTicks,
                           unsigned long long& msSinceLastTick);

// How far the camera had moved from what this mod last wrote, at the moment the
// hold was released. Anything above the engine's own recompute noise means
// something wrote the camera while the head pose was supposed to be on it, and
// every frame drawn between that write and the release rendered without it.
void RecordHeldIntegrity(float rotDeg, float posUnits, float leanUnits);

// The same, for the window an override is open across a single view-matrix
// build. Anything non-zero here is an engine write this mod is about to undo.
void RecordTransientIntegrity(float rotDeg, float posUnits);

// Which camera the game is running this tick, identified by its state object's
// vtable. Reported as a count of switches per second: a view that keeps flipping
// between two cameras and one camera that keeps moving look identical on screen
// and need completely different fixes.
void RecordCameraState(void* playerCamera, uintptr_t currentState);

// Every view-matrix build the engine makes, player or not, tagged with whether
// the head pose was on the camera at the time.
//
// This is the mechanism itself rather than one of its symptoms: a render pass
// that samples the camera while the pose is off lights its frame from a camera
// pointing somewhere else, and unlike the visible flicker it can be counted in
// any camera state, in any scene, without needing the artifact to reproduce.
void RecordBuildExposure(bool poseWasOn, bool isPlayerCamera);

// Start a 1 Hz reporter on its own thread. Deliberately NOT driven by the camera
// tick and NOT gated behind arming an audit: VATS and the pause menu stop the
// camera ticking while the renderer keeps running, so a reporter on the tick goes
// silent in exactly the states that most need measuring - and a silent log reads
// as a clean pass. It is also not gated on anything the player has to press
// first, because the fault being hunted is one they see constantly.
//
// Reports the gates as well as the verdicts: a run where the head pose was near
// centre cannot tell the two states apart, and that has to be visible rather
// than reading as a clean pass.
void StartFrameVerdictReporter();
void StopFrameVerdictReporter();

// Write the ring buffer to HeadTracking.trace.txt. Pressed AFTER the fault is
// seen, so it records continuously rather than needing to be armed first.
void DumpFrameVerdictTrace();

} // namespace Fallout4HT
