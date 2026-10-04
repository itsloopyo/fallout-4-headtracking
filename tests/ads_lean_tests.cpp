// SPDX-License-Identifier: MIT
//
// The lean while aiming down sights: core's LeanHandover drives the split between the
// camera and the first-person skeleton, and CameraShareOfLean turns the two shares into what
// the camera adds to an eye the skeleton has already moved. These run the split the way the
// camera hook does, frame by frame, and hold the eye to the one place the lean puts it.

#include "game/fallout4_types.h"
#include "hooks/camera_math.h"

#include <cameraunlock/ads/aim_mode.h>
#include <cameraunlock/ads/lean_handover.h>
#include <cameraunlock/camera/zoom_compensation.h>

#include <cmath>
#include <cstdio>
#include <initializer_list>

using namespace Fallout4HT;
using cameraunlock::ads::AimMode;
using cameraunlock::ads::LeanHandover;
using cameraunlock::ads::LeanShares;
using cameraunlock::math::Vec3;

namespace {

int g_failures = 0;

void Check(bool cond, const char* what) {
    if (!cond) {
        std::printf("  FAIL: %s\n", what);
        ++g_failures;
    }
}

bool Near(const NiPoint3& a, const NiPoint3& b, float tolerance = 1e-3f) {
    return fabsf(a.x - b.x) <= tolerance && fabsf(a.y - b.y) <= tolerance && fabsf(a.z - b.z) <= tolerance;
}

bool IsZero(const Vec3& v) { return v.x == 0.0f && v.y == 0.0f && v.z == 0.0f; }

// A lean with every axis in it, inside the default limits, in tracker metres.
const Vec3 kLean(0.21f, -0.07f, -0.18f);
// Its parts across the aim and along it (tracker z negative is forward).
const Vec3 kAcross(0.21f, -0.07f, 0.0f);
const Vec3 kAlong(0.0f, 0.0f, -0.18f);
// Turned and pitched well off level, so an axis that only agrees with a level view shows.
const NiMatrix33 kPitched = NiMatrix33::FromEulerAngles(1.1f, -0.6f, 0.2f);
// Near the origin: at the game's world coordinates a float resolves only to ~0.008 units.
const NiPoint3 kUnleanedEye(12.5f, -40.25f, 120.5f);

// Past the longer of the two AdsFade legs, so a transition has settled.
constexpr unsigned long long kSettledMs = 400;

NiPoint3 World(const Vec3& meters, float scale = 1.0f) {
    return TrackerLeanToWorldUnits(kPitched, meters.x * scale, meters.y * scale, meters.z * scale);
}

// One frame as the camera hook runs it: the rig is moved by its share held to last frame's
// clamp, the camera update reads the eye off the rig, and the held pose then adds the
// camera's share of the clamped whole.
struct Frame {
    NiPoint3 rigWorld;
    NiPoint3 cameraOffset;
    NiPoint3 eye;
};

Frame Apply(const LeanShares& shares, float lastScale, float scale) {
    Frame f;
    f.rigWorld = World(shares.rig, lastScale);
    const NiPoint3 whole = World(shares.camera + shares.rig);
    f.cameraOffset = CameraShareOfLean(whole, f.rigWorld, scale);
    f.eye = NiPoint3(kUnleanedEye.x + f.rigWorld.x + f.cameraOffset.x, kUnleanedEye.y + f.rigWorld.y + f.cameraOffset.y,
                     kUnleanedEye.z + f.rigWorld.z + f.cameraOffset.z);
    return f;
}

void HipLeavesTheRigAlone() {
    std::printf("at the hip the camera carries the whole lean and the rig is never written\n");
    LeanHandover handover;
    const LeanShares shares = handover.Update(kLean, kTrackerForward, false, false, true, 0);
    Check(IsZero(shares.rig), "the rig's share is zero");
    const Frame f = Apply(shares, 1.0f, 1.0f);
    Check(f.rigWorld.x == 0.0f && f.rigWorld.y == 0.0f && f.rigWorld.z == 0.0f,
          "nothing is written to the skeleton");
    Check(Near(f.cameraOffset, World(kLean)), "the camera adds the whole lean");
    Check(!handover.Stop(), "stopping at the hip has no rig to release");
}

void SightsUpPutTheLeanAcrossTheAimOnTheRig() {
    std::printf("sights up, the rig carries the lean across the aim and the camera the lean along it\n");
    LeanHandover handover;
    handover.Update(kLean, kTrackerForward, false, false, true, 0);
    handover.Update(kLean, kTrackerForward, true, false, true, 1);
    const LeanShares shares = handover.Update(kLean, kTrackerForward, true, false, true, 1 + kSettledMs);
    Check(Near(World(shares.camera), World(kAlong)), "the camera keeps the lean along the aim");
    const Frame f = Apply(shares, 1.0f, 1.0f);
    Check(Near(f.rigWorld, World(kAcross)), "the skeleton is moved by the lean across the aim");
    Check(Near(f.cameraOffset, World(kAlong)),
          "the camera adds only the lean along the aim, which keeps the eye on the sight line");
}

void TheForwardAxisIsTheCamerasForward() {
    std::printf("the axis the lean is split on is the clean camera's forward\n");
    const NiPoint3 expected(kPitched.entry[1][0] * kUnitsPerMeter, kPitched.entry[1][1] * kUnitsPerMeter,
                            kPitched.entry[1][2] * kUnitsPerMeter);
    Check(Near(World(kTrackerForward), expected),
          "the tracker forward lands on row 1 of the clean root, pointing the way it looks");
}

void EverySplitPutsTheEyeInOnePlace() {
    std::printf("with a pitched view, every split of the lean lands the eye in the same place\n");
    const NiPoint3 expected(kUnleanedEye.x + World(kLean).x, kUnleanedEye.y + World(kLean).y,
                            kUnleanedEye.z + World(kLean).z);
    LeanHandover handover;
    handover.Update(kLean, kTrackerForward, false, false, true, 0);
    bool allSame = true;
    bool sawMidway = false;
    // Up, across the whole of the transition, then back down.
    for (unsigned long long t = 1; t <= 1 + 2 * kSettledMs; t += 7) {
        const bool aiming = t <= 1 + kSettledMs;
        const LeanShares shares = handover.Update(kLean, kTrackerForward, aiming, false, true, t);
        if (!IsZero(shares.rig) && !Near(World(shares.rig), World(kAcross))) sawMidway = true;
        if (!Near(Apply(shares, 1.0f, 1.0f).eye, expected)) allSame = false;
    }
    Check(sawMidway, "the walk passed through splits carried by both");
    Check(allSame, "the eye never moved while the lean changed carriers");
}

void TheClampHoldsWhicheverCarrierHasIt() {
    std::printf("the collision clamp holds the eye at the same place whichever carrier holds the lean\n");
    const float scale = 0.4f;
    const NiPoint3 allowed(kUnleanedEye.x + World(kLean, scale).x, kUnleanedEye.y + World(kLean, scale).y,
                           kUnleanedEye.z + World(kLean, scale).z);

    LeanHandover hip;
    Check(Near(Apply(hip.Update(kLean, kTrackerForward, false, false, true, 0), scale, scale).eye, allowed),
          "on the camera, the eye stops where the clamp allows");

    LeanHandover aimed;
    aimed.Update(kLean, kTrackerForward, false, false, true, 0);
    aimed.Update(kLean, kTrackerForward, true, false, true, 1);
    const LeanShares onRig = aimed.Update(kLean, kTrackerForward, true, false, true, 1 + kSettledMs);
    Check(Near(Apply(onRig, scale, scale).eye, allowed), "on the rig, the eye stops at the same place");

    // The rig moves before this frame's clamp runs, so it is held to last frame's. A wall
    // that appears this frame is met by the camera pulling the eye back at once.
    const Frame tightened = Apply(onRig, 1.0f, scale);
    Check(Near(tightened.eye, allowed), "a clamp that tightens this frame still stops the eye in time");
    Check(tightened.cameraOffset.x != 0.0f || tightened.cameraOffset.y != 0.0f || tightened.cameraOffset.z != 0.0f,
          "the camera makes up what the rig over-carried");
}

void TrueFreeLookKeepsTheLeanOnTheCamera() {
    std::printf("in true free look the camera keeps the lean through the aim\n");
    LeanHandover handover;
    handover.Update(kLean, kTrackerForward, false, true, true, 0);
    handover.Update(kLean, kTrackerForward, true, true, true, 1);
    const LeanShares shares = handover.Update(kLean, kTrackerForward, true, true, true, 1 + kSettledMs);
    Check(IsZero(shares.rig), "the rig carries nothing");
    Check(Near(Apply(shares, 1.0f, 1.0f).cameraOffset, World(kLean)), "the camera adds the whole lean");
}

void NoRigEasesTheLeanAcrossTheAimOut() {
    std::printf("sights up in a camera with no first-person skeleton, the lean across the aim eases out\n");
    LeanHandover handover;
    handover.Update(kLean, kTrackerForward, false, false, false, 0);
    handover.Update(kLean, kTrackerForward, true, false, false, 1);
    const LeanShares shares = handover.Update(kLean, kTrackerForward, true, false, false, 1 + kSettledMs);
    Check(IsZero(shares.rig), "the rig carries nothing");
    Check(Near(World(shares.camera), World(kAlong)), "the camera keeps only the lean along the aim");
}

// Sights locked is a first-person mode: in third person the camera keeps the whole lean
// through the aim, including the frame the view leaves first person with the sights up
// and the rig still carrying the lean across the aim.
void ThirdPersonKeepsTheWholeLeanWhileAiming() {
    std::printf("sights up in third person, the camera keeps the whole lean\n");
    const NiPoint3 expected(kUnleanedEye.x + World(kLean).x, kUnleanedEye.y + World(kLean).y,
                            kUnleanedEye.z + World(kLean).z);
    LeanHandover third;
    bool thirdWhole = true;
    for (unsigned long long t = 0; t <= 2 * kSettledMs; t += 7) {
        const LeanShares shares = ShareLean(third, kLean, false, t > 0, false, false, t);
        const Frame f = Apply(shares, 1.0f, 1.0f);
        if (!IsZero(shares.rig) || !Near(f.cameraOffset, World(kLean))) thirdWhole = false;
    }
    Check(thirdWhole, "the camera adds the whole lean and the rig carries nothing");

    LeanHandover handover;
    ShareLean(handover, kLean, true, false, false, true, 0);
    ShareLean(handover, kLean, true, true, false, true, 1);
    const LeanShares firstPerson = ShareLean(handover, kLean, true, true, false, true, 1 + kSettledMs);
    Check(Near(World(firstPerson.rig), World(kAcross)), "in first person the rig carries the lean across the aim");
    const LeanShares left = ShareLean(handover, kLean, false, true, false, false, 2 + kSettledMs);
    Check(IsZero(left.rig), "leaving first person with the sights up takes the lean off the rig");
    Check(Near(Apply(left, 1.0f, 1.0f).cameraOffset, World(kLean)),
          "the first third-person frame puts the whole lean on the camera");
    bool eyeHeld = true;
    for (unsigned long long t = 3 + kSettledMs; t <= 3 + 2 * kSettledMs; t += 7) {
        if (!Near(Apply(ShareLean(handover, kLean, true, true, false, true, t), 1.0f, 1.0f).eye, expected)) {
            eyeHeld = false;
        }
    }
    Check(eyeHeld, "back in first person the eye keeps the whole lean while it moves back onto the rig");
    Check(Near(World(ShareLean(handover, kLean, true, true, false, true, 4 + 2 * kSettledMs).rig), World(kAcross)),
          "back in first person the rig carries the lean across the aim again");
}

void TheRigCarriesTheLeanAtTheHip() {
    std::printf("at the hip, outside free look, the rig carries the lean across the aim\n");
    LeanHandover locked;
    ShareLean(locked, kLean, true, false, false, true, 0);
    const LeanShares hip = ShareLean(locked, kLean, true, false, false, true, kSettledMs);
    Check(Near(World(hip.rig), World(kAcross)), "the rig carries the lean across the aim with the sights down");
    Check(Near(World(hip.camera), World(kAlong)), "the camera keeps only the lean along the aim");
    const LeanShares raised = ShareLean(locked, kLean, true, true, false, true, kSettledMs + 1);
    Check(Near(World(raised.rig), World(hip.rig)), "raising the sights moves nothing between the two");

    LeanHandover freeLook;
    ShareLean(freeLook, kLean, true, false, true, true, 0);
    const LeanShares free = ShareLean(freeLook, kLean, true, false, true, true, kSettledMs);
    Check(IsZero(free.rig) && Near(World(free.camera), World(kLean)), "in free look the camera keeps the whole lean");

    LeanHandover noRig;
    ShareLean(noRig, kLean, true, false, false, false, 0);
    const LeanShares bare = ShareLean(noRig, kLean, true, false, false, false, kSettledMs);
    Check(IsZero(bare.rig) && Near(World(bare.camera), World(kLean)),
          "with no skeleton to carry it the camera keeps the whole lean at the hip");
}

void StoppingReleasesTheRig() {
    std::printf("sights down, position off and suspend all release the rig\n");
    for (const char* why : {"position off", "suspend"}) {
        LeanHandover handover;
        handover.Update(kLean, kTrackerForward, false, false, true, 0);
        handover.Update(kLean, kTrackerForward, true, false, true, 1);
        handover.Update(kLean, kTrackerForward, true, false, true, 1 + kSettledMs);
        Check(handover.Stop(), why);
        Check(IsZero(handover.Update(kLean, kTrackerForward, false, false, true, 2 + kSettledMs).rig),
              "the next frame starts at the hip");
    }
    LeanHandover handover;
    handover.Update(kLean, kTrackerForward, false, false, true, 0);
    handover.Update(kLean, kTrackerForward, true, false, true, 1);
    handover.Update(kLean, kTrackerForward, true, false, true, 1 + kSettledMs);
    handover.Update(kLean, kTrackerForward, false, false, true, 2 + kSettledMs);
    Check(IsZero(handover.Update(kLean, kTrackerForward, false, false, true, 2 + 2 * kSettledMs).rig),
          "sights down hands the lean back to the camera");
}

void RepeatTicksReplaceTheRigWrite() {
    std::printf("a camera tick that finds this frame's rig write still on replaces it\n");
    const uintptr_t rig = 0x1000;
    RigWrite last;
    bool allCarried = true;
    for (int frame = 0; frame < 12; ++frame) {
        // The player update rewrites the root from the player once a frame.
        NiPoint3 local(-1.6f + 0.25f * frame, 5.6f - 0.1f * frame, 0.1f);
        const NiPoint3 player = local;
        // One, two or three camera ticks, each with its own share; the last is a
        // release in every third frame.
        const int ticks = 1 + frame % 3;
        for (int tick = 0; tick < ticks; ++tick) {
            const bool release = frame % 3 == 2 && tick == ticks - 1;
            const NiPoint3 world = release ? NiPoint3() : NiPoint3(4.0f + tick, -2.5f * (tick + 1), 1.0f + frame);
            const NiPoint3 delta = RigDelta(last, rig, local, world);
            local = NiPoint3(local.x + delta.x, local.y + delta.y, local.z + delta.z);
            last.rig = rig;
            last.applied = world;
            last.localAfter = local;
            if (!Near(local, NiPoint3(player.x + world.x, player.y + world.y, player.z + world.z), 1e-5f)) {
                allCarried = false;
            }
        }
    }
    Check(allCarried, "the root carries exactly the latest tick's share, never the sum of the frame's");

    RigWrite other;
    other.rig = 0x2000;
    other.applied = NiPoint3(9.0f, 9.0f, 9.0f);
    other.localAfter = NiPoint3(1.0f, 2.0f, 3.0f);
    Check(Near(RigDelta(other, rig, NiPoint3(1.0f, 2.0f, 3.0f), NiPoint3(1.0f, 0.0f, 0.0f)), NiPoint3(1.0f, 0.0f, 0.0f)),
          "a write to another skeleton is not taken back off this one");
}

// What the camera hook does with a mode: only whether it is a free look mode reaches
// the lean, the weapon and the shares.
bool FreeLook(AimMode mode) { return cameraunlock::ads::IsFreeLook(mode); }

LeanShares SettledSightsUp(AimMode mode, const Vec3& lean) {
    LeanHandover handover;
    handover.Update(lean, kTrackerForward, false, FreeLook(mode), true, 0);
    handover.Update(lean, kTrackerForward, true, FreeLook(mode), true, 1);
    return handover.Update(lean, kTrackerForward, true, FreeLook(mode), true, 1 + kSettledMs);
}

void TheTwoFreeLookModesShareOneLean() {
    std::printf("free look with a marker and true free look put the lean in the same place\n");
    const LeanShares marker = SettledSightsUp(AimMode::FreeLookMarker, kLean);
    const LeanShares plain = SettledSightsUp(AimMode::TrueFreeLook, kLean);
    Check(marker.camera.x == plain.camera.x && marker.camera.y == plain.camera.y && marker.camera.z == plain.camera.z,
          "the camera's share, which is what the aim hook and the marker are handed, is the same");
    Check(IsZero(marker.rig) && IsZero(plain.rig), "the rig carries nothing in either");
}

void TheMarkerIsMode2WithTheSightsUp() {
    std::printf("the aim marker is asked for only in free look with a marker, and follows the sights\n");
    cameraunlock::ads::AdsFade sights;
    sights.Update(false, 0);
    bool followed = true;
    for (unsigned long long t = 1; t <= 1 + 2 * kSettledMs; t += 7) {
        const float sightsUp = 1.0f - sights.Update(t <= 1 + kSettledMs, t);
        if (cameraunlock::ads::AimMarkerOpacity(AimMode::FreeLookMarker, sightsUp) != sightsUp) followed = false;
        if (cameraunlock::ads::AimMarkerOpacity(AimMode::SightsLocked, sightsUp) != 0.0f) followed = false;
        if (cameraunlock::ads::AimMarkerOpacity(AimMode::TrueFreeLook, sightsUp) != 0.0f) followed = false;
        if (cameraunlock::ads::AimMarkerOpacity(AimMode::StockSights, sightsUp) != 0.0f) followed = false;
    }
    Check(followed, "its opacity is the sights' fade in mode 2 and zero in modes 1, 3 and 4");
    Check(cameraunlock::ads::AimMarkerOpacity(AimMode::FreeLookMarker, 0.0f) == 0.0f, "there is none at the hip");
    Check(cameraunlock::ads::AimMarkerOpacity(AimMode::FreeLookMarker, 1.0f) == 1.0f, "it is opaque with the sights up");
}

void LeaningInIsNeverCutShortAtTheHip() {
    std::printf("a lean in of the whole forward limit is applied in full at the hip, at any zoom\n");
    const Vec3 in(0.0f, 0.0f, -0.4f);
    for (const float zoom : {1.0f, 0.5f, 0.25f, 0.1f}) {
        const Vec3 scaled = cameraunlock::camera::ScaleLeanForZoom(in, kTrackerForward, zoom);
        for (const AimMode mode : {AimMode::SightsLocked, AimMode::FreeLookMarker, AimMode::TrueFreeLook}) {
            LeanHandover handover;
            const LeanShares shares = handover.Update(scaled, kTrackerForward, false, FreeLook(mode), true, 0);
            Check(shares.camera.z == -0.4f && IsZero(shares.rig), "the camera carries all 0.4 m of it");
        }
    }
}

void LeaningInIsNeverCutShortWithTheSightsUp() {
    std::printf("sights up, a lean in is applied in full, at any zoom, in every mode\n");
    const Vec3 in(0.21f, -0.07f, -0.4f);
    for (const float zoom : {1.0f, 0.5f, 0.25f, 0.1f}) {
        const Vec3 scaled = cameraunlock::camera::ScaleLeanForZoom(in, kTrackerForward, zoom);
        Check(scaled.z == in.z, "the zoom leaves the lean along the view alone");
        Check(fabsf(scaled.x - in.x * zoom) < 1e-6f && fabsf(scaled.y - in.y * zoom) < 1e-6f,
              "and scales the lean across it");
        for (const AimMode mode : {AimMode::SightsLocked, AimMode::FreeLookMarker, AimMode::TrueFreeLook}) {
            const LeanShares shares = SettledSightsUp(mode, scaled);
            Check(shares.camera.z == -0.4f, "the camera carries all 0.4 m of the lean in");
            Check(shares.rig.z == 0.0f, "the lean along the aim is never handed to the rig");
        }
    }
}

// A pose with every axis in it: degrees, then tracker metres.
struct Pose {
    float yaw = 20.0f, pitch = -8.0f, roll = 11.0f;
    float x = 0.21f, y = -0.07f, z = -0.18f;
};

// One tick of the camera hook's stock sights step.
float Ease(cameraunlock::ads::AdsFade& fade, AimMode mode, bool sightsUp, unsigned long long nowMs, Pose& p) {
    return EaseStockSightsPose(fade, mode, sightsUp, nowMs, p.yaw, p.pitch, p.x, p.y, p.z);
}

bool Untouched(const Pose& p) {
    const Pose whole;
    return p.yaw == whole.yaw && p.pitch == whole.pitch && p.roll == whole.roll && p.x == whole.x && p.y == whole.y &&
           p.z == whole.z;
}

bool ScaledBy(const Pose& p, float share) {
    const Pose whole;
    return p.yaw == whole.yaw * share && p.pitch == whole.pitch * share && p.x == whole.x * share &&
           p.y == whole.y * share && p.z == whole.z * share && p.roll == whole.roll;
}

void StockSightsLeavesTheHipAlone() {
    std::printf("stock sights: at the hip the pose passes through untouched\n");
    cameraunlock::ads::AdsFade fade;
    bool whole = true;
    for (unsigned long long t = 0; t <= kSettledMs; t += 7) {
        Pose p;
        if (Ease(fade, AimMode::StockSights, false, t, p) != 1.0f || !Untouched(p)) whole = false;
    }
    Check(whole, "yaw, pitch, roll and the lean are the tracker's");
}

void StockSightsEasesThePoseOutAndKeepsRoll() {
    std::printf("stock sights: sights up, yaw, pitch and the lean ease out and roll stays\n");
    cameraunlock::ads::AdsFade fade;
    Pose hip;
    Ease(fade, AimMode::StockSights, false, 0, hip);
    bool scaled = true;
    bool sawMidway = false;
    float last = 1.0f;
    bool falling = true;
    for (unsigned long long t = 1; t < 1 + kSettledMs; t += 7) {
        Pose p;
        const float share = Ease(fade, AimMode::StockSights, true, t, p);
        if (!ScaledBy(p, share)) scaled = false;
        if (share > 0.0f && share < 1.0f) sawMidway = true;
        if (share > last) falling = false;
        last = share;
    }
    Check(sawMidway, "the walk passed through the transition");
    Check(scaled, "mid-transition yaw, pitch and the lean are scaled by the fade and roll is untouched");
    Check(falling, "the share only falls while the sights stay up");
    Pose up;
    Check(Ease(fade, AimMode::StockSights, true, 1 + kSettledMs, up) == 0.0f, "the share settles at zero");
    Check(up.yaw == 0.0f && up.pitch == 0.0f && up.x == 0.0f && up.y == 0.0f && up.z == 0.0f,
          "yaw, pitch and all three lean axes are zero");
    Check(up.roll == Pose().roll, "roll is the tracker's roll");
}

// More than a whole leg moves between two ticks 7 ms apart, and far less than the step a
// leg restarted from its own end would make.
constexpr float kLargestStep = 0.12f;

void StockSightsReversalsContinue() {
    std::printf("stock sights: a reversal mid-transition continues from where it was\n");
    for (const bool byModeKey : {false, true}) {
        cameraunlock::ads::AdsFade fade;
        Pose p;
        float last = Ease(fade, AimMode::StockSights, false, 0, p);
        bool continuous = true;
        bool reversed = false;
        // Sights up for 70 ms, then the aim button is released or the mode key steps to
        // sights locked, then back 70 ms later.
        for (unsigned long long t = 1; t <= 211; t += 7) {
            const bool away = t > 71 && t <= 141;
            const bool sightsUp = byModeKey || !away;
            const AimMode mode = byModeKey && away ? AimMode::SightsLocked : AimMode::StockSights;
            Pose q;
            const float share = Ease(fade, mode, sightsUp, t, q);
            if (fabsf(share - last) > kLargestStep) continuous = false;
            if (away && share > last) reversed = true;
            last = share;
        }
        Check(reversed, byModeKey ? "the mode key turned the fade round" : "the aim button turned the fade round");
        Check(continuous, "nothing stepped");
    }
}

void TheOtherModesNeverEaseThePose() {
    std::printf("in the other three modes the stock sights share is 1, sights up or down\n");
    for (const AimMode mode : {AimMode::SightsLocked, AimMode::FreeLookMarker, AimMode::TrueFreeLook}) {
        cameraunlock::ads::AdsFade fade;
        bool whole = true;
        for (unsigned long long t = 0; t <= 2 * kSettledMs; t += 7) {
            Pose p;
            if (Ease(fade, mode, t > kSettledMs, t, p) != 1.0f || !Untouched(p)) whole = false;
        }
        Check(whole, "the pose passes through untouched");
    }
}

void StockSightsSharesTheLeanAsSightsLockedDoes() {
    std::printf("stock sights hands the lean over as sights locked does, with TrueFreeLook set beside it too\n");
    const AimMode besideFreeLook = cameraunlock::ads::DecodeAimMode(true, false, true);
    Check(besideFreeLook == AimMode::StockSights && !FreeLook(besideFreeLook) &&
              !FreeLook(cameraunlock::ads::DecodeAimMode(true, true, true)),
          "StockSights beside TrueFreeLook is not free look");
    LeanHandover locked;
    LeanHandover stock;
    bool same = true;
    for (unsigned long long t = 0; t <= 1 + kSettledMs; t += 7) {
        const LeanShares a = locked.Update(kLean, kTrackerForward, t > 0, FreeLook(AimMode::SightsLocked), true, t);
        const LeanShares b = stock.Update(kLean, kTrackerForward, t > 0, FreeLook(besideFreeLook), true, t);
        if (a.camera.x != b.camera.x || a.camera.y != b.camera.y || a.camera.z != b.camera.z || a.rig.x != b.rig.x ||
            a.rig.y != b.rig.y || a.rig.z != b.rig.z) {
            same = false;
        }
    }
    Check(same, "the camera's and the rig's shares are those of sights locked at every step");
}

void StockSightsLeavesNoLeanInWithTheSightsUp() {
    std::printf("stock sights: a lean in is whole at the hip and zero with the sights up\n");
    cameraunlock::ads::AdsFade fade;
    Pose hip;
    hip.z = -0.4f;
    Ease(fade, AimMode::StockSights, false, 0, hip);
    Check(hip.z == -0.4f, "the hip keeps all 0.4 m");
    Pose rising;
    Ease(fade, AimMode::StockSights, true, 1, rising);
    Pose up;
    up.z = -0.4f;
    Ease(fade, AimMode::StockSights, true, 1 + kSettledMs, up);
    Check(up.z == 0.0f, "nothing of it is left with the sights up");
}

}  // namespace

int main() {
    std::printf("Fallout4HeadTracking ADS lean tests\n"
                "===================================\n");
    HipLeavesTheRigAlone();
    SightsUpPutTheLeanAcrossTheAimOnTheRig();
    TheForwardAxisIsTheCamerasForward();
    EverySplitPutsTheEyeInOnePlace();
    TheClampHoldsWhicheverCarrierHasIt();
    TrueFreeLookKeepsTheLeanOnTheCamera();
    NoRigEasesTheLeanAcrossTheAimOut();
    ThirdPersonKeepsTheWholeLeanWhileAiming();
    TheRigCarriesTheLeanAtTheHip();
    StoppingReleasesTheRig();
    RepeatTicksReplaceTheRigWrite();
    TheTwoFreeLookModesShareOneLean();
    TheMarkerIsMode2WithTheSightsUp();
    LeaningInIsNeverCutShortAtTheHip();
    LeaningInIsNeverCutShortWithTheSightsUp();
    StockSightsLeavesTheHipAlone();
    StockSightsEasesThePoseOutAndKeepsRoll();
    StockSightsReversalsContinue();
    TheOtherModesNeverEaseThePose();
    StockSightsSharesTheLeanAsSightsLockedDoes();
    StockSightsLeavesNoLeanInWithTheSightsUp();

    if (g_failures == 0) {
        std::printf("All tests passed!\n");
        return 0;
    }
    std::printf("%d test(s) FAILED\n", g_failures);
    return 1;
}
