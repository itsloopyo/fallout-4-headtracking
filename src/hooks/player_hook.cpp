// SPDX-License-Identifier: MIT

#include "pch.h"
#include "player_hook.h"
#include "camera_hook.h"
#include "camera_math.h"
#include "camera_snapshot.h"
#include "hook_slot.h"
#include "core/constants.h"
#include "core/logging.h"
#include "core/seh_guard.h"
#include "core/vector_math.h"
#include "diagnostics/ab_switches.h"
#include "diagnostics/frame_verdict.h"
#include "game/fallout4_types.h"

#include <cameraunlock/memory/rtti_vtable.h>

namespace Fallout4HT {
namespace {

// virtual void Update(float deltaTime)
typedef void (__fastcall *PlayerUpdate_t)(void* thisPlayer, float deltaTime);
PlayerUpdate_t g_originalUpdate = nullptr;
HookSlot g_updateHook;

std::recursive_mutex g_cameraMutationMutex;

struct SavedCameraState {
    NiMatrix33 rootLocal;
    NiMatrix33 rootWorld;
    NiMatrix33 niCamLocal;
    NiMatrix33 niCamWorld;
    NiPoint3 rootLocalPosition;
    NiPoint3 rootWorldPosition;
    NiPoint3 niCamLocalPosition;
    NiPoint3 niCamWorldPosition;
    bool restoreRoot;
    bool restoreNiCamLocal;
    bool valid;
};

float Length3(const NiPoint3& a, const NiPoint3& b) {
    return Distance3(&a.x, &b.x);
}

SavedCameraState g_heldState{};
CameraRootSnapshots g_heldSnapshot{};
bool g_renderPoseHeld = false;
OverrideReference g_heldReference{};

thread_local int t_cleanScopeDepth = 0;
thread_local int t_cameraTickDepth = 0;

// Every actor's shots and auto-aim run through the two functions this mod hooks,
// so "is this the player" has to be answerable inside them. Without it an NPC
// firing across the street was measured as the player's shot, and the verdict on
// whether aim was decoupled flipped run to run depending on whether anything
// nearby happened to be in a gunfight.
std::atomic<void*> g_playerActor{nullptr};

// The camera tick runs NESTED INSIDE the player update - PlayerCamera::Update is
// called from PlayerCharacter::Update + 0x17EE, proved from the return addresses
// - so both depths are non-zero there and the innermost one is the honest label.
uint8_t CurrentScope() {
    if (t_cameraTickDepth > 0) return kScopeCameraTick;
    if (t_cleanScopeDepth > 0) return kScopeCleanGameplay;
    return kScopeOther;
}

thread_local SavedCameraState t_transientState{};
thread_local OverrideReference t_transientReference{};
thread_local int t_overrideDepth = 0;

void FillReference(const SavedCameraState& saved, const NiMatrix33& trackedNiCam,
                   const NiPoint3& trackedPos, float leanUnits, OverrideReference& out) {
    for (int i = 0; i < 3; ++i) {
        out.cleanFwd[i] = saved.niCamWorld.entry[0][i];
        out.trackedFwd[i] = trackedNiCam.entry[0][i];
    }
    out.cleanPos[0] = saved.niCamWorldPosition.x;
    out.cleanPos[1] = saved.niCamWorldPosition.y;
    out.cleanPos[2] = saved.niCamWorldPosition.z;
    out.trackedPos[0] = trackedPos.x;
    out.trackedPos[1] = trackedPos.y;
    out.trackedPos[2] = trackedPos.z;
    out.leanUnits = leanUnits;
    out.scope = CurrentScope();
    out.valid = true;
}

// Count attempts that had to wait for another camera transaction. The mutex
// serializes them before either one touches the shared nodes.
std::atomic<int> g_overrideDepth{0};
std::atomic<uint64_t> g_overrideOverlaps{0};

std::atomic<uint64_t> g_skipNoSnapshot{0};
std::atomic<uint64_t> g_skipNoRenderPose{0};
std::atomic<uint64_t> g_skipRotateFailed{0};

bool ApplyRenderPose(const CameraRootSnapshots& snap, const RenderPose& pose,
                     bool includeRoot, SavedCameraState& saved,
                     OverrideReference& reference) {
    static std::atomic<uint64_t> s_faults{0};
    __try {
        NiMatrix33* rootLocal = LocalRotationOf(snap.cameraRoot);
        NiMatrix33* rootWorld = WorldRotationOf(snap.cameraRoot);
        NiMatrix33* niCamLocal = LocalRotationOf(snap.niCamera);
        NiMatrix33* niCamWorld = WorldRotationOf(snap.niCamera);

        saved.rootLocal = *rootLocal;
        saved.rootWorld = *rootWorld;
        saved.niCamLocal = *niCamLocal;
        saved.niCamWorld = *niCamWorld;
        saved.rootLocalPosition = *LocalTranslationOf(snap.cameraRoot);
        saved.rootWorldPosition = *WorldTranslationOf(snap.cameraRoot);
        saved.niCamLocalPosition = *LocalTranslationOf(snap.niCamera);
        saved.niCamWorldPosition = *WorldTranslationOf(snap.niCamera);

        const NiMatrix33 trackedRoot =
            pose.rotation.cameraFrame * saved.rootWorld * pose.rotation.worldFrame;
        const NiMatrix33 trackedNiCam = ComposeChildWorld(
            saved.niCamLocal, trackedRoot, saved.niCamWorld, saved.rootWorld);
        *niCamWorld = trackedNiCam;
        if (includeRoot) {
            *rootWorld = trackedRoot;
            *rootLocal = pose.rotation.cameraFrame * saved.rootLocal
                       * pose.rotation.worldFrame;
        } else {
            *niCamLocal = SolveChildLocal(
                trackedNiCam, saved.rootWorld, saved.niCamLocal,
                saved.niCamWorld, saved.rootWorld);
        }

        if (pose.hasPosition) {
            const NiPoint3 offset = TrackerLeanToWorldUnits(
                saved.rootWorld, pose.positionX, pose.positionY, pose.positionZ);
            if (includeRoot) {
                *LocalTranslationOf(snap.cameraRoot) = NiPoint3(
                    saved.rootLocalPosition.x + offset.x,
                    saved.rootLocalPosition.y + offset.y,
                    saved.rootLocalPosition.z + offset.z);
                *WorldTranslationOf(snap.cameraRoot) = NiPoint3(
                    saved.rootWorldPosition.x + offset.x,
                    saved.rootWorldPosition.y + offset.y,
                    saved.rootWorldPosition.z + offset.z);
            } else {
                // niCamera's local translation is expressed in cameraRoot's
                // frame, so the lean goes in unrotated - but through the same
                // axis mapping the world offset above uses. Spelling it out
                // separately is how the two came to disagree about the sign of
                // the forward axis, and since the engine rebuilds the world
                // translation from this one, the disagreement was invisible in
                // every dump and visible only on screen.
                const NiPoint3 localLean = TrackerLeanToCameraLocalUnits(
                    pose.positionX, pose.positionY, pose.positionZ);
                *LocalTranslationOf(snap.niCamera) = NiPoint3(
                    saved.niCamLocalPosition.x + localLean.x,
                    saved.niCamLocalPosition.y + localLean.y,
                    saved.niCamLocalPosition.z + localLean.z);
            }
            *WorldTranslationOf(snap.niCamera) = NiPoint3(
                saved.niCamWorldPosition.x + offset.x,
                saved.niCamWorldPosition.y + offset.y,
                saved.niCamWorldPosition.z + offset.z);
        }

        saved.restoreRoot = includeRoot;
        // Always true off the held path, whatever the rotation toggle says: the
        // lean writes the local translation unconditionally, and not restoring
        // it would let the lean accumulate frame on frame.
        saved.restoreNiCamLocal = !includeRoot;
        saved.valid = true;
        FillReference(saved, trackedNiCam, *WorldTranslationOf(snap.niCamera),
                      pose.hasPosition ? Length3(*WorldTranslationOf(snap.niCamera),
                                                 saved.niCamWorldPosition) : 0.0f,
                      reference);
        return true;
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "render pose apply", s_faults)) {
    }
    return false;
}

void RestoreCameraState(const CameraRootSnapshots& snap, SavedCameraState& saved) {
    static std::atomic<uint64_t> s_faults{0};
    __try {
        if (saved.restoreRoot) {
            *LocalRotationOf(snap.cameraRoot) = saved.rootLocal;
            *WorldRotationOf(snap.cameraRoot) = saved.rootWorld;
            *LocalTranslationOf(snap.cameraRoot) = saved.rootLocalPosition;
            *WorldTranslationOf(snap.cameraRoot) = saved.rootWorldPosition;
        }
        if (saved.restoreNiCamLocal) {
            *LocalRotationOf(snap.niCamera) = saved.niCamLocal;
            *LocalTranslationOf(snap.niCamera) = saved.niCamLocalPosition;
        }
        *WorldRotationOf(snap.niCamera) = saved.niCamWorld;
        *WorldTranslationOf(snap.niCamera) = saved.niCamWorldPosition;
    } __except (SehAbsorbAnyException("render pose restore", s_faults)) {
    }
    saved.valid = false;
}

// How far the camera has drifted from what this mod last wrote into it.
//
// Both windows this mod opens ask the same question of the same two fields, so
// they share the measurement and differ only in which reference they are held
// against and what they do with the answer. Returns false if the node could not
// be read, in which case there is nothing to report.
struct CameraDrift {
    float rotationDeg;
    float positionUnits;
};

bool MeasureDriftFromReference(uintptr_t niCamera, const OverrideReference& reference,
                               const char* where, std::atomic<uint64_t>& faults,
                               CameraDrift& out) {
    __try {
        const NiMatrix33* live = WorldRotationOf(niCamera);
        const NiPoint3* livePos = WorldTranslationOf(niCamera);
        out.rotationDeg = DegreesBetweenUnit(live->entry[0], reference.trackedFwd);
        out.positionUnits = Distance3(&livePos->x, reference.trackedPos);
        return true;
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), where, faults)) {
    }
    return false;
}

// Did anything move the camera while the head pose was held on it?
//
// This is the one question about the held window that has no ambiguity. The
// verdict taken at a build cannot separate "the engine legitimately moved the
// camera" from "the engine wiped our offset", because both leave the camera
// somewhere that is neither of the two states we know about. Here there is no
// such doubt: whatever the engine did between the hold and now, if the node no
// longer holds the exact bytes this mod wrote, something else wrote them, and
// every frame drawn after that point rendered without the head pose.
void CheckHeldIntegrity() {
    if (!g_heldReference.valid) return;
    static std::atomic<uint64_t> s_faults{0};
    CameraDrift drift{};
    if (MeasureDriftFromReference(g_heldSnapshot.niCamera, g_heldReference,
                                  "held integrity", s_faults, drift)) {
        RecordHeldIntegrity(drift.rotationDeg, drift.positionUnits, g_heldReference.leanUnits);
    }
}

void CheckTransientIntegrity(const CameraRootSnapshots& snap) {
    if (!t_transientReference.valid) return;
    static std::atomic<uint64_t> s_faults{0};
    CameraDrift drift{};
    if (MeasureDriftFromReference(snap.niCamera, t_transientReference,
                                  "transient integrity", s_faults, drift)) {
        RecordTransientIntegrity(drift.rotationDeg, drift.positionUnits);
    }
}

void ReleaseRenderPoseLocked() {
    if (!g_renderPoseHeld) return;
    CheckHeldIntegrity();
    RestoreCameraState(g_heldSnapshot, g_heldState);
    g_renderPoseHeld = false;
    g_heldReference.valid = false;
}

bool HoldLatestRenderPoseLocked(const CameraRootSnapshots& snap) {
    // The pose has to be left ON the camera between builds, not merely applied
    // around each of the player's view-matrix builds. Measured with it applied
    // only around builds: all 209 player builds a second came out tracked, and
    // verified so against the state this mod itself had just written - and the
    // view still did not move, because the engine rebuilds what the renderer
    // finally consumes from the scene graph after our window has closed. So the
    // rotation must live in the graph, and taking it back out at the right
    // moments is what the clean scopes and the pause watchdog are for.
    RenderPose pose{};
    if (!LatestRenderPose(pose)) return false;
    if (!ApplyRenderPose(snap, pose, false, g_heldState, g_heldReference)) return false;
    g_heldSnapshot = snap;
    g_renderPoseHeld = true;
    return true;
}

// A frame is about 10 ms, so three missed frames is a pause rather than a hitch.
// Low enough that the snap back to body aim lands with the VATS interface rather
// than visibly after it; high enough that an ordinary stutter does not trip it,
// and self-correcting if one does, because the next camera tick puts the pose
// straight back.
constexpr unsigned long long kPausedAfterMs = 40;

std::atomic<bool> g_watchdogRunning{false};
std::thread g_watchdogThread;

void PauseWatchdog() {
    bool wasPaused = false;
    while (g_watchdogRunning.load(std::memory_order_relaxed)) {
        Sleep(8);
        unsigned long long ticks = 0;
        unsigned long long sinceMs = 0;
        GetCameraTickLiveness(ticks, sinceMs);
        if (ticks == 0) continue;               // nothing has ticked yet
        const bool paused = sinceMs >= kPausedAfterMs;
        if (paused && !wasPaused) {
            ReleaseRenderPose();
            Log::Line("game stopped updating (%llu ms since the last camera tick):"
                      " head pose taken off the camera so the frozen view is the"
                      " one the body is aimed at", sinceMs);
        }
        wasPaused = paused;
    }
}

void __fastcall PlayerUpdateHook(void* thisPlayer, float deltaTime) {
    g_playerActor.store(thisPlayer, std::memory_order_relaxed);
    BeginCleanCameraScope();
    __try {
        g_originalUpdate(thisPlayer, deltaTime);
    } __finally {
        EndCleanCameraScope();
    }
}

}  // namespace

std::recursive_mutex& CameraMutationMutex() {
    return g_cameraMutationMutex;
}

void* PlayerActor() { return g_playerActor.load(std::memory_order_relaxed); }

bool IsPlayerActor(void* actor) {
    return actor != nullptr && actor == g_playerActor.load(std::memory_order_relaxed);
}

bool RenderPoseIsHeld() { return g_renderPoseHeld; }

namespace {

// Unwind everything BeginTrackedOverride took before it decided it could not
// proceed, and count the reason. Exactly reverses the acquisition order at the
// top of that function; it was spelled out at each of the three abort points,
// which is three chances for one of them to fall out of step with the others and
// leak the mutex for the rest of the session.
bool AbandonOverride(std::atomic<uint64_t>& reasonCounter) {
    reasonCounter.fetch_add(1, std::memory_order_relaxed);
    --t_overrideDepth;
    g_cameraMutationMutex.unlock();
    g_overrideDepth.fetch_sub(1, std::memory_order_acq_rel);
    return false;
}

}  // namespace

bool BeginTrackedOverride(CameraRootSnapshots& snap, OverridePath& path,
                          OverrideReference& reference) {
    path = OverridePath::None;
    reference.valid = false;
    if (g_overrideDepth.fetch_add(1, std::memory_order_acq_rel) != 0) {
        g_overrideOverlaps.fetch_add(1, std::memory_order_relaxed);
    }
    g_cameraMutationMutex.lock();

    // Read before the depth check so a nested build gets a usable snapshot
    // rather than the caller's uninitialised stack.
    const bool haveSnapshot =
        GetCameraRootSnapshots(snap) && snap.cameraRoot != 0 && snap.niCamera != 0;

    if (t_overrideDepth++ != 0) {
        path = OverridePath::Nested;
        return true;
    }

    if (!haveSnapshot) return AbandonOverride(g_skipNoSnapshot);

    if (g_renderPoseHeld) {
        path = OverridePath::Held;
        reference = g_heldReference;
        reference.scope = CurrentScope();
        return true;
    }

    RenderPose pose{};
    if (!LatestRenderPose(pose)) return AbandonOverride(g_skipNoRenderPose);

    if (!ApplyRenderPose(snap, pose, true, t_transientState, t_transientReference)) {
        return AbandonOverride(g_skipRotateFailed);
    }
    path = OverridePath::Transient;
    reference = t_transientReference;
    return true;
}

uint64_t TakeOverrideOverlaps() {
    return g_overrideOverlaps.exchange(0, std::memory_order_relaxed);
}

OverrideSkips TakeOverrideSkips() {
    OverrideSkips s;
    s.noSnapshot = g_skipNoSnapshot.exchange(0, std::memory_order_relaxed);
    s.noRenderPose = g_skipNoRenderPose.exchange(0, std::memory_order_relaxed);
    s.rotateFailed = g_skipRotateFailed.exchange(0, std::memory_order_relaxed);
    return s;
}

void EndTrackedOverride(const CameraRootSnapshots& snap) {
    if (--t_overrideDepth == 0 && t_transientState.valid) {
        // Restoring writes back the bytes that were there before the override.
        // If the engine wrote the camera during the build, that restore throws
        // the engine's own write away - and in third person the camera position
        // is exactly the sort of thing the engine recomputes, so a clobber there
        // would fight the game for the camera every frame. Checked rather than
        // assumed: it is the one way this design can move the camera without any
        // instrument on our own state noticing.
        CheckTransientIntegrity(snap);
        RestoreCameraState(snap, t_transientState);
    }
    g_cameraMutationMutex.unlock();
    g_overrideDepth.fetch_sub(1, std::memory_order_acq_rel);
}

bool HoldLatestRenderPose(const CameraRootSnapshots& snap) {
    std::lock_guard<std::recursive_mutex> lock(g_cameraMutationMutex);
    if (g_renderPoseHeld) ReleaseRenderPoseLocked();

    // Put it back at the earliest possible moment. The camera tick is called from
    // the middle of PlayerCharacter::Update (+0x17EE of it), so deferring to the
    // end of that scope would leave the head pose off the camera for the whole
    // rest of the player update - and every render pass that samples the camera
    // in that window lights its frame from the body-aimed camera. Holding here
    // costs nothing: the pose goes on niCamera, and game logic reads cameraRoot.
    return HoldLatestRenderPoseLocked(snap);
}

void ReleaseRenderPose() {
    std::lock_guard<std::recursive_mutex> lock(g_cameraMutationMutex);
    ReleaseRenderPoseLocked();
}

void StartPauseWatchdog() {
    if (g_watchdogRunning.exchange(true)) return;
    g_watchdogThread = std::thread(PauseWatchdog);
}

void StopPauseWatchdog() {
    if (!g_watchdogRunning.exchange(false)) return;
    if (g_watchdogThread.joinable()) g_watchdogThread.join();
}

void BeginCameraTickScope() { ++t_cameraTickDepth; }
void EndCameraTickScope() { --t_cameraTickDepth; }

// The held pose lives on niCamera; cameraRoot keeps the body's orientation.
//
// Which of those a piece of game logic reads decides whether it needs the pose
// taken off, and the two callers here differ:
//
//   PlayerCharacter::Update reads cameraRoot, so it already sees the body. It
//     must NOT have the pose stripped. Doing that WAS the flicker: measured over
//     the whole update, 8.7-12.6% of frames came out lit from the body-aimed
//     camera, because the render thread samples the camera without this lock.
//     Leaving the pose on: 0.00% across 24 headings in both views.
//
//   The fire path reads niCamera - proved by logging both nodes at the instant
//     the launch is built, with cameraRoot 0.02 deg off the body and niCamera
//     0.03 deg off the head - so it MUST have the pose stripped or every shot
//     flies where the player is looking. Its window is one call, not a frame.
//
// Whichever scope takes the pose off is the one that puts it back, at its own
// exit rather than the outermost, so a fire path nested inside a player update
// costs microseconds of exposure instead of the rest of the update.
thread_local int t_poseReleasedAtDepth = 0;

void OpenCleanScope(bool stripPose) {
    g_cameraMutationMutex.lock();
    ++t_cleanScopeDepth;
    if (!stripPose || t_poseReleasedAtDepth != 0 || !g_renderPoseHeld) return;
    t_poseReleasedAtDepth = t_cleanScopeDepth;
    ReleaseRenderPoseLocked();
}

void BeginCleanCameraScope() { OpenCleanScope(AbSwitches::StripPoseInCleanScope()); }

// The aim path needs a body-aimed niCamera, and it needs it CHEAPLY.
//
// The full release-and-reapply costs about 69 us a call - it restores eight
// matrices and then recomputes the whole pose from the current snapshot - and
// the auto-aim solver runs every frame, so paying that per frame put ~1% of view
// builds back on the un-tracked camera and the mod straight back to reporting
// unhealthy. The solver itself only takes 16.7 us; nearly all of that window was
// our own bookkeeping.
//
// So this swaps the two fields the aim code actually reads - niCamera's world
// rotation and world translation - and puts them back. Two small copies each
// way, a window measured in microseconds rather than tens of them, and nothing
// else in the scene graph is touched.
thread_local int t_aimScopeDepth = 0;
thread_local NiMatrix33 t_aimSavedRotation{};
thread_local NiPoint3 t_aimSavedPosition{};
thread_local bool t_aimSwapped = false;

void BeginAimCleanScope() {
    g_cameraMutationMutex.lock();
    ++t_cleanScopeDepth;
    if (t_aimScopeDepth++ != 0) return;      // already clean for an outer aim scope
    t_aimSwapped = false;
    if (!g_renderPoseHeld || !g_heldState.valid || g_heldSnapshot.niCamera == 0) return;

    static std::atomic<uint64_t> s_faults{0};
    __try {
        NiMatrix33* world = WorldRotationOf(g_heldSnapshot.niCamera);
        NiPoint3* position = WorldTranslationOf(g_heldSnapshot.niCamera);
        t_aimSavedRotation = *world;
        t_aimSavedPosition = *position;
        *world = g_heldState.niCamWorld;
        *position = g_heldState.niCamWorldPosition;
        t_aimSwapped = true;
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "aim clean scope", s_faults)) {
    }
}

void EndAimCleanScope() {
    if (--t_aimScopeDepth == 0 && t_aimSwapped) {
        static std::atomic<uint64_t> s_faults{0};
        __try {
            *WorldRotationOf(g_heldSnapshot.niCamera) = t_aimSavedRotation;
            *WorldTranslationOf(g_heldSnapshot.niCamera) = t_aimSavedPosition;
        } __except (SehAbsorbAccessViolation(GetExceptionCode(), "aim clean scope", s_faults)) {
        }
        t_aimSwapped = false;
    }
    --t_cleanScopeDepth;
    g_cameraMutationMutex.unlock();
}

void EndCleanCameraScope() {
    const bool restorePose = t_poseReleasedAtDepth == t_cleanScopeDepth;
    // Depth drops before the restore so the writes it makes are attributed the
    // way they were before this scope split, not to the scope being closed.
    --t_cleanScopeDepth;
    if (restorePose) {
        t_poseReleasedAtDepth = 0;
        if (!g_renderPoseHeld) {
            CameraRootSnapshots snap{};
            if (!GetCameraRootSnapshots(snap) || snap.cameraRoot == 0 || snap.niCamera == 0
                || !HoldLatestRenderPoseLocked(snap)) {
                Log::Line("ERROR: failed to resume the render pose after a clean camera scope");
            }
        }
    }
    g_cameraMutationMutex.unlock();
}

bool InstallPlayerHook(uintptr_t moduleBase, const TextSection& text) {
    HMODULE gameModule = GetModuleHandleA(GAME_EXE);
    if (!gameModule) {
        Log::Line("ERROR: no game module handle for the player hook");
        return false;
    }

    cameraunlock::memory::VtableInfo vtInfo{};
    if (!cameraunlock::memory::FindVtableFromRTTI(gameModule, kRTTI_PlayerCharacter, vtInfo, 1)) {
        Log::Line("ERROR: PlayerCharacter RTTI not found - gameplay camera isolation unavailable");
        return false;
    }

    // Actor::Update sits past VtableInfo's captured window, so the slot is read
    // straight off the vtable. Checked against .text rather than the module: the
    // module also spans .rdata, where every vtable lives, so a slot read past the
    // end of a shorter vtable would land on one of those and pass a module check.
    const uintptr_t updateFunc = *reinterpret_cast<uintptr_t*>(
        vtInfo.vtable_address + kVtableIndex_ActorUpdate * sizeof(uintptr_t));
    if (updateFunc < text.start || updateFunc >= text.start + text.size) {
        Log::Line("ERROR: PlayerCharacter vtable[0x%X] is not code: 0x%llX",
                  kVtableIndex_ActorUpdate, static_cast<unsigned long long>(updateFunc));
        return false;
    }

    Log::Line("PlayerCharacter::Update at RVA 0x%llX",
              static_cast<unsigned long long>(updateFunc - moduleBase));

    if (!g_updateHook.Install(reinterpret_cast<void*>(updateFunc),
                              reinterpret_cast<void*>(&PlayerUpdateHook),
                              reinterpret_cast<void**>(&g_originalUpdate),
                              "PlayerCharacter::Update")) {
        return false;
    }
    Log::Line("player hook installed - game logic sees the body's camera, not the head's");
    return true;
}

void RemovePlayerHook() {
    g_updateHook.Remove();
}

} // namespace Fallout4HT
