// SPDX-License-Identifier: MIT

#include "pch.h"
#include "camera_hook.h"
#include "aim_decoupling.h"
#include "camera_math.h"
#include "camera_snapshot.h"
#include "crosshair_hook.h"
#include "hit_marker_hook.h"
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
#include "game/fov_settings.h"
#include "game/game_state.h"
#include "lean_trace.h"
#include "ads_lean.h"

#include <cameraunlock/ads/lean_handover.h>
#include <cameraunlock/camera/zoom_compensation.h>
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

void PublishRenderPose(const RenderPose& pose, bool haveRotation) {
    std::lock_guard<std::mutex> lock(g_renderPoseMutex);
    g_hasRenderPose = haveRotation;
    if (!haveRotation) return;

    const uint64_t tick = g_latestRenderPose.tick + 1;
    g_latestRenderPose = pose;
    g_latestRenderPose.tick = tick;
}

// The lean while aiming down sights (the shooter-ads-handling skill).
//
// Sights locked, the default: as the sights come up the lean is handed over from
// the camera to the first-person skeleton, the rig the eye, the arms, the weapon
// and its projectile node all hang off, so the sights stay in front of the eye
// and the round leaves from where the eye is. At the hip the camera carries it
// all, and the first-person pass, which draws from the eye the skeleton gives it,
// keeps the weapon where it is in the frame.
//
// True free look: the camera keeps the whole lean through the aim, and the
// skeleton's geometry is moved back by the camera's share after the camera update
// has read the eye, so the weapon stays put in the world while the head moves
// around it. Toggling rides a fade of its own, so the weapon slides rather than
// steps.
//
// Both are first-person modes. Outside the first-person camera the camera keeps
// the whole lean through the aim.
//
// Camera-thread only.
cameraunlock::ads::LeanHandover g_leanHandover;
cameraunlock::ads::AdsFade g_freeLookFade;

struct LeanSplit {
    float cameraX, cameraY, cameraZ;
    float rigX, rigY, rigZ;
    NiPoint3 rigWorld;
    uintptr_t rig;
    // 0 in sights locked, 1 in true free look.
    float weaponShare;
};

struct AdsReport {
    bool aiming;
    bool firstPerson;
    bool haveRig;
    bool freeLook;
    bool valid;
};
AdsReport g_lastAdsReport{};
uint64_t g_lastAdsSampleMs = 0;
constexpr uint64_t kAdsSampleIntervalMs = 2000;

bool CleanRootRotation(void* camera, NiMatrix33& out) {
    static std::atomic<uint64_t> s_faults{0};
    __try {
        CameraNodes nodes{};
        if (!ResolveCameraNodes(camera, nodes)) return false;
        out = *WorldRotationOf(nodes.cameraRoot);
        return true;
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "lean basis", s_faults)) {
    }
    return false;
}

void ReportAds(bool aiming, bool firstPerson, bool haveRig, bool freeLook, const LeanSplit& split) {
    const AdsReport now{aiming, firstPerson, haveRig, freeLook, true};
    if (!g_lastAdsReport.valid || now.aiming != g_lastAdsReport.aiming ||
        now.firstPerson != g_lastAdsReport.firstPerson || now.haveRig != g_lastAdsReport.haveRig ||
        now.freeLook != g_lastAdsReport.freeLook) {
        const char* what = !aiming      ? "sights down: the lean moves the view"
                           : !firstPerson ? "sights up outside first person: the camera keeps the whole lean"
                           : freeLook    ? "sights up, true free look: the lean stays on the camera and the weapon stays put"
                           : haveRig     ? "sights up, sights locked: the lean is carried on the first-person skeleton"
                                         : "sights up with no first-person skeleton: the lean eases out";
        Log::Line("ADS: %s", what);
        g_lastAdsReport = now;
    }
    const uint64_t nowMs = GetTickCount64();
    if (aiming && nowMs - g_lastAdsSampleMs >= kAdsSampleIntervalMs) {
        g_lastAdsSampleMs = nowMs;
        Log::Line("ADS lean: camera (%.3f %.3f %.3f) m, rig (%.3f %.3f %.3f) m, rig moved (%.2f %.2f %.2f)"
                  " units, clamp %.3f, weapon share %.2f",
                  split.cameraX, split.cameraY, split.cameraZ, split.rigX, split.rigY, split.rigZ,
                  split.rigWorld.x, split.rigWorld.y, split.rigWorld.z, LeanScale(), split.weaponShare);
    }
}

void StopLean() {
    // Not writing is the whole release: the engine rewrites the skeleton root
    // from the player every frame (measured: an offset written in one frame is
    // gone by the next camera update).
    g_leanHandover.Stop();
    g_freeLookFade.Reset();
    g_lastAdsReport.valid = false;
}

LeanSplit SplitLean(void* camera, bool active, float x, float y, float z, bool freeLook) {
    LeanSplit out{x, y, z, 0.0f, 0.0f, 0.0f, NiPoint3(), 0, 0.0f};
    if (!active) {
        StopLean();
        return out;
    }
    const uintptr_t player = reinterpret_cast<uintptr_t>(PlayerActor());
    const bool aiming = AdsLean::IsAiming(player);
    const bool firstPerson = AdsLean::IsFirstPersonCamera(camera);
    NiMatrix33 rootWorld;
    const uintptr_t rig = CleanRootRotation(camera, rootWorld) && firstPerson ? AdsLean::FirstPersonRig(player) : 0;
    const uint64_t nowMs = GetTickCount64();

    const cameraunlock::ads::LeanShares shares = ShareLean(g_leanHandover, cameraunlock::math::Vec3(x, y, z),
                                                           firstPerson, aiming, freeLook, rig != 0, nowMs);
    const float freeLookScale = g_freeLookFade.Update(freeLook, nowMs);
    out.cameraX = shares.camera.x;
    out.cameraY = shares.camera.y;
    out.cameraZ = shares.camera.z;
    out.rigX = shares.rig.x;
    out.rigY = shares.rig.y;
    out.rigZ = shares.rig.z;
    out.rig = rig;
    out.weaponShare = rig != 0 ? 1.0f - freeLookScale : 0.0f;
    // Held to what the collision clamp allowed last frame: the rig has to move
    // before the camera update, which is before this frame's clamp can run.
    const float scale = LeanScale();
    out.rigWorld = TrackerLeanToWorldUnits(rootWorld, out.rigX * scale, out.rigY * scale, out.rigZ * scale);
    ReportAds(aiming, firstPerson, rig != 0, freeLook, out);
    return out;
}

// After the held pose is on: in true free look, move the skeleton's geometry back
// by what the camera added to the eye, so the weapon keeps its place in the world.
void ApplyFreeLookWeapon(const LeanSplit& split) {
    if (split.rig == 0 || split.weaponShare <= 0.0f) {
        NoteWeaponShift(0, NiPoint3());
        return;
    }
    const NiPoint3 offset = HeldCameraOffset();
    const NiPoint3 shift(-offset.x * split.weaponShare, -offset.y * split.weaponShare,
                         -offset.z * split.weaponShare);
    NoteWeaponShift(AdsLean::ShiftWeapon(split.rig, shift) ? split.rig : 0, shift);
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
    lean_trace::Reset();
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

        // A narrow field of view magnifies everything in the frame, head
        // tracking included, so a scope or iron sights would otherwise sweep the
        // view further for the same head angle and read as the mod's
        // sensitivity jumping the moment the player aims. Scaling here, before
        // the rotation is composed, is what makes everything downstream agree:
        // the render injection, the crosshair projection built from the basis
        // that was written, and the diagnostics all describe one camera.
        //
        // Yaw, pitch and the lean translate the image, so all three take it.
        // Roll rotates the image about the view axis by the same angle at every
        // field of view, so it does not.
        //
        // The frustum this uses was read at the end of the previous tick, since
        // the engine has not computed this one yet. A zoom is therefore followed
        // one frame late, which is invisible next to the zoom animation itself.
        const float zoom = FovSettings::CurrentZoomFactor();
        if (zoom != 1.0f) {
            yaw = cameraunlock::camera::ScaleAngleForZoom(yaw, zoom);
            pitch = cameraunlock::camera::ScaleAngleForZoom(pitch, zoom);
            posX *= zoom;
            posY *= zoom;
            posZ *= zoom;
        }
    }
    const HeadRotation head = haveRotation
        ? ComputeHeadRotation(yaw, pitch, roll, worldSpaceYaw)
        : HeadRotation{};

    RecordTickPose(haveRotation, mod.IsPositionActive(), hasPosition, posX, posY, posZ,
                   sqrtf(yaw * yaw + pitch * pitch + roll * roll));

    CameraMutationMutex().lock();
    ReleaseRenderPose();
    ClearWeaponShift();
    if (!hasPosition) lean_trace::Reset();
    const LeanSplit split = SplitLean(thisCamera, hasPosition && haveRotation, posX, posY, posZ,
                                      mod.IsTrueFreeLook());
    RenderPose pose{};
    pose.rotation = head;
    pose.positionX = split.cameraX;
    pose.positionY = split.cameraY;
    pose.positionZ = split.cameraZ;
    pose.rigX = split.rigX;
    pose.rigY = split.rigY;
    pose.rigZ = split.rigZ;
    pose.rigWorld = split.rigWorld;
    pose.hasPosition = hasPosition;
    pose.deltaTime = mod.LastPipelineSample().deltaTime;
    pose.cameraState = *reinterpret_cast<const uintptr_t*>(
        reinterpret_cast<uintptr_t>(thisCamera) + TESCameraOffsets::CurrentState);
    PublishRenderPose(pose, haveRotation);
    AdsLean::CarryOnRig(split.rig, split.rigWorld);

    // Call original - engine positions the camera and computes worldToCam
    BeginCameraTickScope();
    g_originalUpdate(thisCamera);
    EndCameraTickScope();

    // Nothing is applied this frame, so there is nothing for the fire path to
    // undo either.
    if (gate == TickGate::Suppressed || !haveRotation) {
        CameraNodes nodes{};
        PublishHitMarkerView(ResolveCameraNodes(thisCamera, nodes) ? nodes.niCamera : 0);
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
            PublishHitMarkerView(0);
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
        if (HoldLatestRenderPose(snapshot)) {
            ApplyFreeLookWeapon(split);
        } else {
            // Every tick fails alike once it fails at all, so the count doubles
            // between lines rather than writing one a frame on the game thread.
            static uint64_t s_holdFailures = 0;
            if ((++s_holdFailures & (s_holdFailures - 1)) == 0) {
                Log::Line("ERROR: failed to hold the current render pose (%llu so far)",
                          static_cast<unsigned long long>(s_holdFailures));
            }
        }
        PublishHitMarkerView(nodes.niCamera);

    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "camera hook", s_faults)) {
        PublishHitMarkerView(0);
        PublishCameraRootSnapshots(CameraRootSnapshots{});
    }
    CameraMutationMutex().unlock();

    // Off the camera rather than off the pose, so the basis is visible without a
    // tracker connected and without loading a save. Self-limiting: once, then
    // only when the rendered FOV moves.
    FovSettings::NoteRenderedFrustum(snapshot.frustumRight, snapshot.frustumTop);
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
    if (!g_hasRenderPose || GameState::IsTrackingMenuOpen()) return false;
    pose = g_latestRenderPose;
    return true;
}

bool InstallCameraHook() {
    Log::Line("Installing camera hook...");

    if (!GameState::Initialize()) {
        Log::Line("ERROR: Game state detection init failed; camera tracking not installed");
        return false;
    }

    // Scans .data for the engine's own FOV settings, so it runs here on the
    // init thread rather than on a camera tick. Failing leaves the compensation
    // off and says so; it never guesses a reference.
    FovSettings::Initialize();
    lean_trace::Initialize();

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
    AdsLean::Install(gameModule);
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
    GameState::Shutdown();

    if (g_updateHook.IsInstalled()) {
        g_updateHook.Remove();
        Log::Line("Camera hook removed");
    }
}

} // namespace Fallout4HT
