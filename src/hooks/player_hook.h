// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <mutex>

#include "camera_math.h"
#include "camera_snapshot.h"
#include "module_scan.h"

namespace Fallout4HT {

// Serializes gameplay camera reads with the render pose.
bool InstallPlayerHook(uintptr_t moduleBase, const TextSection& text);
void RemovePlayerHook();

// How a view build came to be looking at a head-tracked camera. Which one a
// build took decides which code is responsible for what it rendered, so a
// verdict on the frame is only actionable alongside it.
enum class OverridePath : uint8_t {
    None = 0,       // no override - the build ran against whatever was there
    Held = 1,       // the pose was already applied and left in place
    Transient = 2,  // applied for the length of this build, then restored
    Nested = 3,     // a build inside a build; the outer one owns the state
};

// The camera state as it was the last time this mod set it: what the engine had
// (clean) and what was written over it (tracked). Taken at the moment of the
// write rather than from the camera tick's snapshot, which is up to a frame old -
// and a frame is long enough for the player to walk several units and turn
// several degrees, so a verdict measured against it accuses the mod of dropping
// tracking every time the camera legitimately moves.
struct OverrideReference {
    // Which of the mod's own windows the build landed in. A build that happens
    // while game logic is deliberately being shown the clean camera is a
    // different animal from one in the camera tick's own gap, and until this was
    // recorded the two were indistinguishable in every trace.
    uint8_t scope;
    float cleanFwd[3];
    float trackedFwd[3];
    float cleanPos[3];
    float trackedPos[3];
    float leanUnits;
    bool valid;
};

// Applies the current pose when a view build occurs inside a clean gameplay
// scope. If the camera is already held tracked for rendering, these only lock.
bool BeginTrackedOverride(CameraRootSnapshots& snap, OverridePath& path,
                          OverrideReference& reference);
void EndTrackedOverride(const CameraRootSnapshots& snap);

// Keeps the current pose in the renderer-facing NiCamera until the next clean
// gameplay scope or camera update. cameraRoot remains body-aimed for collision,
// VATS, and other game logic.
bool HoldLatestRenderPose(const CameraRootSnapshots& snap);
void ReleaseRenderPose();

// Scope codes for OverrideReference::scope.
enum : uint8_t {
    kScopeNone = 0,
    kScopeCameraTick = 1,   // inside PlayerCamera::Update, between release and hold
    kScopeCleanGameplay = 2, // inside a clean scope: player update, fire, auto-aim
    kScopeOther = 3,
};

// Takes the head pose back off the camera whenever the game stops updating.
//
// VATS, the Pip-Boy and the pause menu all freeze the game while the renderer
// carries on drawing, so whatever the view was pointing at is what the player is
// left staring at. With head tracking that is wherever their head happened to be
// looking, not where the body is aimed - which is why VATS could open with the
// target it had just selected sitting off screen. Nothing else can be used as
// the signal: the camera state does not change when VATS opens, and the camera
// simply stops being ticked.
void StartPauseWatchdog();
void StopPauseWatchdog();

// Marks the camera tick's own window so a build inside it can be told apart from
// one inside a clean gameplay scope.
void BeginCameraTickScope();
void EndCameraTickScope();

// Serialises against the render-pose writers for the length of a piece of game
// logic. Nests; the scope that took the pose off is the one that puts it back.
//
// Two flavours, because the two callers want opposite things and sharing one
// switch between them cost aim decoupling entirely:
//
//   BeginCleanCameraScope  wraps PlayerCharacter::Update, and does NOT take the
//     pose off. It once did, and that WAS the flicker: the window is a whole
//     game update, ~10 ms of every frame, and the render thread samples the
//     camera without this lock, so 8-12% of frames came out lit from the
//     body-aimed camera.
//
//   BeginAimCleanScope / EndAimCleanScope wrap the FIRE PATH - one call per
//     shot - and swap niCamera's world rotation and translation for the
//     body-aimed ones. They have to: the shot direction comes from niCamera,
//     which is the node the held pose lives on, so a shot taken with the pose
//     still on flies wherever the player is LOOKING.
//
// Nothing that runs every frame may use the aim scope. Even swapping two fields
// leaves niCamera body-aimed for the length of the wrapped call, and wrapping
// the once-per-frame auto-aim solver that way put ~1% of view builds back on the
// un-tracked camera. The solver's result is corrected at fire time instead.
void BeginCleanCameraScope();
void EndCleanCameraScope();
void BeginAimCleanScope();
void EndAimCleanScope();

// True for the actor the player is driving, as last seen by the player update.
// The aim hooks run for every actor in the game and must only touch the
// player's camera.
bool IsPlayerActor(void* actor);

// The actor the player is driving, or null before the first player update.
void* PlayerActor();

std::recursive_mutex& CameraMutationMutex();

// Is the head pose currently on the renderer-facing camera? Read without taking
// the lock: this is a diagnostic sample of a window, not a decision.
bool RenderPoseIsHeld();

// Number of render transactions that had to serialize behind another one.
uint64_t TakeOverrideOverlaps();

struct OverrideSkips {
    uint64_t noSnapshot;
    uint64_t noRenderPose;
    uint64_t rotateFailed;
};
OverrideSkips TakeOverrideSkips();

} // namespace Fallout4HT
