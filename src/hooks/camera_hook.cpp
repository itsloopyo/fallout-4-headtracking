// SPDX-License-Identifier: MIT

#include "pch.h"
#include "camera_hook.h"
#include "aim_decoupling.h"
#include "camera_math.h"
#include "camera_snapshot.h"
#include "crosshair_hook.h"
#include "hook_slot.h"
#include "module_scan.h"
#include "player_hook.h"
#include "view_matrix_hook.h"
#include "core/logging.h"
#include "core/mod.h"
#include "core/seh_guard.h"
#include "core/vector_math.h"
#include "diagnostics/frame_verdict.h"
#include "diagnostics/pose_trace.h"
#include "diagnostics/render_audit.h"
#include "game/fallout4_types.h"
#include "game/game_state.h"

#include <cameraunlock/memory/pattern_scanner.h>
#include <cameraunlock/memory/rtti_vtable.h>

namespace Fallout4HT {
namespace {

// TESCamera::Update is void Update() - no deltaTime parameter.
typedef void (__fastcall *PlayerCameraUpdate_t)(void* thisCamera);
PlayerCameraUpdate_t g_originalUpdate = nullptr;
HookSlot g_updateHook;

// PlayerCamera::Update is inherited unchanged from TESCamera (PlayerCamera does
// NOT override vtable[3]), so the hooked function address is the shared base
// impl that fires for every TESCamera-derived camera (menu, VATS, etc.). We
// capture the PlayerCamera vtable at install time and filter on it inside the
// hook so tracking only applies to the actual player camera object.
uintptr_t g_playerCamVtable = 0;

std::mutex g_renderPoseMutex;
RenderPose g_latestRenderPose{};
bool g_hasRenderPose = false;

void PublishRenderPose(const HeadRotation& rotation, bool haveRotation,
                       bool hasPosition, float x, float y, float z) {
    std::lock_guard<std::mutex> lock(g_renderPoseMutex);
    g_hasRenderPose = haveRotation;
    if (!haveRotation) return;

    g_latestRenderPose.rotation = rotation;
    g_latestRenderPose.positionX = x;
    g_latestRenderPose.positionY = y;
    g_latestRenderPose.positionZ = z;
    g_latestRenderPose.hasPosition = hasPosition;
}

// Camera-thread only. File scope rather than function statics: a local static
// with a constructor makes the enclosing function require unwinding, which SEH
// forbids in the same function as a __try.
NiPoint3 g_previousForward;
bool g_hasPreviousForward = false;

// Row 1 of a cameraRoot world rotation is its forward axis. These two divide by
// DEG_TO_RAD rather than multiplying by RAD_TO_DEG - see constants.h; the two
// conversions differ in the last digit and this is what the recorded figures
// were measured with.
float AngleBetweenForwards(const NiMatrix33& a, const NiMatrix33& b) {
    return RadiansBetweenUnit(a.entry[1], b.entry[1]) / DEG_TO_RAD;
}

// How far the head-tracked forward moved since the previous tick. A view being
// fought over shows up here as a spike followed by an equal spike back.
float SwingSinceLastTick(const NiMatrix33& trackedRootRot) {
    const NiPoint3 forward(trackedRootRot.entry[1][0], trackedRootRot.entry[1][1],
                           trackedRootRot.entry[1][2]);
    float swingDeg = 0.0f;
    if (g_hasPreviousForward) {
        swingDeg = RadiansBetweenUnit(&forward.x, &g_previousForward.x) / DEG_TO_RAD;
    }
    g_previousForward = forward;
    g_hasPreviousForward = true;
    return swingDeg;
}

void RecordTick(const Mod& mod, float swingDeg, float appliedDeg) {
    const Mod::PipelineSample sample = mod.LastPipelineSample();
    PoseTickRecord record{};
    record.rawYaw = sample.rawYaw;
    record.rawPitch = sample.rawPitch;
    record.interpolatedYaw = sample.interpolatedYaw;
    record.processedYaw = sample.processedYaw;
    record.processedPitch = sample.processedPitch;
    record.deltaTime = sample.deltaTime;
    record.newSample = sample.newSample;
    record.cameraSwingDeg = swingDeg;
    record.appliedDeg = appliedDeg;
    record.rejectedPackets = sample.rejectedPackets;
    record.frozenPackets = sample.frozenPackets;
    RecordPoseTick(record);
}

// End the tick without touching the camera nodes: record it as un-tracked and
// retire the published snapshot so the fire path cannot write through a pointer
// this tick has decided not to vouch for.
//
// The trace entry is the point. A run of ticks that render from the engine's own
// camera IS the view flicking to un-tracked, so it is the one case the trace must
// not be silent about - and it was, until a player reported exactly this and
// every instrument here said the camera was fine.
void RetireTick(const Mod& mod) {
    RecordTick(mod, 0.0f, kNothingApplied);
    PublishCameraRootSnapshots(CameraRootSnapshots{});
}

// The three gates between "the engine ticked a camera" and "head tracking runs
// this frame". They differ in whether they retire the published snapshot, so the
// caller is told which happened rather than just yes/no.
enum class TickGate {
    Track,          // run tracking
    NotOurCamera,   // some other TESCamera - leave the last snapshot alone
    Suppressed,     // ours, but tracking is off / not in gameplay - retire it
};

TickGate ClassifyTick(void* thisCamera) {
    // The hooked address is TESCamera::Update, shared by every derived camera.
    // Only the real PlayerCamera object should receive head tracking.
    if (*reinterpret_cast<uintptr_t*>(thisCamera) != g_playerCamVtable) {
        return TickGate::NotOurCamera;
    }
    if (!Mod::Instance().IsEnabled() || !GameState::IsInGameplay(thisCamera)) {
        return TickGate::Suppressed;
    }
    return TickGate::Track;
}

void __fastcall PlayerCameraUpdateHook(void* thisCamera) {
    const TickGate gate = ClassifyTick(thisCamera);
    if (gate == TickGate::NotOurCamera) {
        g_originalUpdate(thisCamera);
        return;
    }

    // Before the original runs, while the matrix the last frame was rendered
    // with is still in memory.
    AuditRenderedFrame(thisCamera);
    {
        static std::atomic<uint64_t> s_faults{0};
        __try {
            const uintptr_t state = *reinterpret_cast<uintptr_t*>(
                reinterpret_cast<uintptr_t>(thisCamera) + TESCameraOffsets::CurrentState);
            RecordCameraState(thisCamera, state);
        } __except (SehAbsorbAccessViolation(GetExceptionCode(), "camera state", s_faults)) {
        }
    }

    // Publish before the engine update so every view build made inside that
    // update consumes this tick's pose.
    Mod& mod = Mod::Instance();
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
    bool haveRotation = false;
    bool hasPosition = false;
    bool worldSpaceYaw = false;
    if (gate == TickGate::Track) {
        haveRotation = mod.GetProcessedRotation(yaw, pitch, roll);
        hasPosition = mod.GetPositionOffset(posX, posY, posZ);
        worldSpaceYaw = mod.IsWorldSpaceYaw();
    }
    const HeadRotation head = haveRotation
        ? ComputeHeadRotation(yaw, pitch, roll, worldSpaceYaw)
        : HeadRotation{};

    RecordTickPose(haveRotation, hasPosition, posX, posY, posZ,
                   sqrtf(yaw * yaw + pitch * pitch + roll * roll));

    CameraMutationMutex().lock();
    ReleaseRenderPose();
    PublishRenderPose(head, haveRotation, hasPosition, posX, posY, posZ);

    // Call original - engine positions the camera and computes worldToCam
    BeginCameraTickScope();
    g_originalUpdate(thisCamera);
    EndCameraTickScope();

    // Nothing is applied this frame, so there is nothing for the fire path to
    // undo either.
    if (gate == TickGate::Suppressed || !haveRotation) {
        RetireTick(mod);
        CameraMutationMutex().unlock();
        return;
    }

    // Built up locally over the frame, then published atomically via the
    // seqlock at the end. A zeroed snapshot (cameraRoot == 0) is the "invalid
    // frame" marker readers treat as "no data this frame".
    CameraRootSnapshots snapshot{};

    static std::atomic<uint64_t> s_faults{0};
    __try {
        CameraNodes nodes{};
        if (!ResolveCameraNodes(thisCamera, nodes)) {
            // The scene graph is being torn down or rebuilt. Whatever node the
            // last snapshot points at may already be freed.
            RetireTick(mod);
            CameraMutationMutex().unlock();
            return;
        }

        const NiMatrix33 cleanRootWorld = *WorldRotationOf(nodes.cameraRoot);
        const NiMatrix33 cleanRootLocal = *LocalRotationOf(nodes.cameraRoot);
        const NiMatrix33 cleanNiCamWorld = *WorldRotationOf(nodes.niCamera);
        const NiMatrix33 trackedRootWorld =
            head.cameraFrame * cleanRootWorld * head.worldFrame;
        const NiMatrix33 trackedRootLocal =
            head.cameraFrame * cleanRootLocal * head.worldFrame;
        const NiMatrix33 trackedNiCamWorld = ComposeChildWorld(
            *LocalRotationOf(nodes.niCamera), trackedRootWorld,
            cleanNiCamWorld, cleanRootWorld);

        snapshot.cameraRoot = nodes.cameraRoot;
        std::memcpy(snapshot.cleanWorld, cleanRootWorld.entry, sizeof(snapshot.cleanWorld));
        std::memcpy(snapshot.cleanLocal, cleanRootLocal.entry, sizeof(snapshot.cleanLocal));
        snapshot.niCamera = nodes.niCamera;
        snapshot.frustumRight =
            *reinterpret_cast<const float*>(nodes.niCamera + NiCameraOffsets::FrustumRight);
        snapshot.frustumTop =
            *reinterpret_cast<const float*>(nodes.niCamera + NiCameraOffsets::FrustumTop);
        std::memcpy(snapshot.cleanNiCamWorld, cleanNiCamWorld.entry,
                    sizeof(snapshot.cleanNiCamWorld));
        std::memcpy(snapshot.cleanWorldToCam,
                    reinterpret_cast<const NiMatrix44*>(
                        nodes.niCamera + NiCameraOffsets::WorldToCam)->entry,
                    sizeof(snapshot.cleanWorldToCam));

        RecordTick(mod, SwingSinceLastTick(trackedRootWorld),
                   AngleBetweenForwards(trackedRootWorld, cleanRootWorld));

        std::memcpy(snapshot.trackedWorld, trackedRootWorld.entry,
                    sizeof(snapshot.trackedWorld));
        std::memcpy(snapshot.trackedLocal, trackedRootLocal.entry,
                    sizeof(snapshot.trackedLocal));
        std::memcpy(snapshot.trackedNiCamWorld, trackedNiCamWorld.entry,
                    sizeof(snapshot.trackedNiCamWorld));

        // Project the body's aim into the head-tracked view for the crosshair.
        const AimProjection aim = ProjectBodyAimToNdc(
            snapshot.cleanNiCamWorld, snapshot.trackedNiCamWorld,
            snapshot.frustumRight, snapshot.frustumTop);
        snapshot.aimNdcX = aim.ndcX;
        snapshot.aimNdcY = aim.ndcY;
        snapshot.aimValid = aim.valid;

        // Publish the fully-built snapshot in one seqlock-guarded write.
        PublishCameraRootSnapshots(snapshot);
        if (!HoldLatestRenderPose(snapshot)) {
            Log::Line("ERROR: failed to hold the current render pose");
        }

    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "camera hook", s_faults)) {
        PublishCameraRootSnapshots(CameraRootSnapshots{});
    }
    CameraMutationMutex().unlock();
}

// Resolve PlayerCamera's vtable by RTTI and hook the Update slot it inherits
// from TESCamera. Returns false if RTTI discovery fails or the slot does not
// hold a code address.
bool InstallPlayerCameraUpdateHook(HMODULE gameModule, uintptr_t moduleBase,
                                   const TextSection& text) {
    cameraunlock::memory::VtableInfo vtInfo{};
    if (!cameraunlock::memory::FindVtableFromRTTI(gameModule, kRTTI_PlayerCamera, vtInfo, 1)) {
        Log::Line("ERROR: PlayerCamera RTTI not found - refusing to hook");
        return false;
    }
    g_playerCamVtable = vtInfo.vtable_address;

    const uintptr_t updateFunc = *reinterpret_cast<uintptr_t*>(
        vtInfo.vtable_address + kVtableIndex_TESCameraUpdate * sizeof(uintptr_t));
    if (updateFunc < text.start || updateFunc >= text.start + text.size) {
        Log::Line("ERROR: vtable[%d] is not code: 0x%llX",
                  kVtableIndex_TESCameraUpdate, static_cast<unsigned long long>(updateFunc));
        return false;
    }

    Log::Line("PlayerCamera::Update at: 0x%llX (offset: 0x%llX)",
              static_cast<unsigned long long>(updateFunc),
              static_cast<unsigned long long>(updateFunc - moduleBase));

    return g_updateHook.Install(reinterpret_cast<void*>(updateFunc),
                                reinterpret_cast<void*>(&PlayerCameraUpdateHook),
                                reinterpret_cast<void**>(&g_originalUpdate),
                                "PlayerCamera::Update");
}

} // namespace

bool LatestRenderPose(RenderPose& pose) {
    std::lock_guard<std::mutex> lock(g_renderPoseMutex);
    if (!g_hasRenderPose) return false;
    pose = g_latestRenderPose;
    return true;
}

bool InstallCameraHook() {
    Log::Line("Installing camera hook...");

    if (!GameState::Initialize()) {
        Log::Line("WARN: Game state detection init failed");
    }

    HMODULE gameModule = GetModuleHandleA(GAME_EXE);
    if (!gameModule) {
        Log::Line("ERROR: Failed to get game module handle");
        return false;
    }

    uintptr_t moduleBase = 0;
    size_t moduleSize = 0;
    if (!cameraunlock::memory::GetModuleRange(gameModule, moduleBase, moduleSize)) {
        Log::Line("ERROR: Failed to get game module range");
        return false;
    }

    TextSection text{};
    if (!FindTextSection(moduleBase, text)) {
        Log::Line("ERROR: no .text section in the game module");
        return false;
    }

    if (!InstallPlayerCameraUpdateHook(gameModule, moduleBase, text)) return false;
    Log::Line("Camera hook installed successfully");

    // Aim decoupling is mandatory. If it is unavailable, remove the camera hook
    // so head movement cannot redirect shots.
    if (!InstallFirePathHook(text, moduleBase)) {
        RemoveCameraHook();
        return false;
    }

    if (!InstallAutoAimHook(text, moduleBase)) {
        RemoveCameraHook();
        return false;
    }
    if (!InstallViewMatrixHook(text, moduleBase)) {
        RemoveCameraHook();
        return false;
    }
    if (!InstallPlayerHook(moduleBase, text)) {
        RemoveCameraHook();
        return false;
    }
    StartFrameVerdictReporter();
    StartPauseWatchdog();
    InstallCrosshairHook(text, moduleBase);
    return true;
}

void RemoveCameraHook() {
    StopFrameVerdictReporter();
    StopPauseWatchdog();
    ReleaseRenderPose();
    RemoveViewMatrixHook();
    RemovePlayerHook();
    RemoveCrosshairHook();
    RemoveAimDecouplingHooks();

    if (g_updateHook.IsInstalled()) {
        g_updateHook.Remove();
        Log::Line("Camera hook removed");
    }
}

} // namespace Fallout4HT
