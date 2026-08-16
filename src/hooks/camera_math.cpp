// SPDX-License-Identifier: MIT

#include "camera_math.h"
#include "core/constants.h"

namespace Fallout4HT {
namespace {

// The dot of clean forward against tracked forward, i.e. the cosine of the angle
// the head has turned away from body aim. At or below this the aim is level with
// or behind the view plane and the projection below would divide through ~0.
constexpr float kMinAimForwardCosine = 0.01f;

} // namespace

HeadRotation ComputeHeadRotation(float yawDeg, float pitchDeg, float rollDeg,
                                 bool worldSpaceYaw) {
    const float yawRad   = yawDeg   * DEG_TO_RAD;
    const float pitchRad = pitchDeg * DEG_TO_RAD;
    const float rollRad  = rollDeg  * DEG_TO_RAD;

    HeadRotation out;
    // Camera-frame part: roll innermost, then pitch, then local yaw when horizon
    // locking is off.
    out.cameraFrame = NiMatrix33::FromEulerAngles(
        worldSpaceYaw ? 0.0f : yawRad, -pitchRad, rollRad);
    // World-frame part: a rotation about world Z (up). Identity when yaw is
    // already accounted for in the camera frame.
    out.worldFrame = worldSpaceYaw
        ? NiMatrix33::FromEulerAngles(yawRad, 0.0f, 0.0f)
        : NiMatrix33();
    return out;
}

NiMatrix33 Transpose(const NiMatrix33& m) {
    NiMatrix33 out;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            out.entry[i][j] = m.entry[j][i];
    return out;
}

namespace {

float SumSquaredDifference(const NiMatrix33& a, const NiMatrix33& b) {
    float total = 0.0f;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            const float d = a.entry[i][j] - b.entry[i][j];
            total += d * d;
        }
    }
    return total;
}

bool ChildLocalMultipliesLeft(const NiMatrix33& childLocal,
                              const NiMatrix33& childWorld,
                              const NiMatrix33& parentWorld) {
    return SumSquaredDifference(childLocal * parentWorld, childWorld) <=
           SumSquaredDifference(parentWorld * childLocal, childWorld);
}

}  // namespace

NiMatrix33 ComposeChildWorld(const NiMatrix33& childLocal,
                             const NiMatrix33& parentWorld,
                             const NiMatrix33& observedChildWorld,
                             const NiMatrix33& observedParentWorld) {
    return ChildLocalMultipliesLeft(childLocal, observedChildWorld, observedParentWorld)
               ? childLocal * parentWorld
               : parentWorld * childLocal;
}

NiMatrix33 SolveChildLocal(const NiMatrix33& desiredChildWorld,
                           const NiMatrix33& parentWorld,
                           const NiMatrix33& observedChildLocal,
                           const NiMatrix33& observedChildWorld,
                           const NiMatrix33& observedParentWorld) {
    return ChildLocalMultipliesLeft(
               observedChildLocal, observedChildWorld, observedParentWorld)
               ? desiredChildWorld * Transpose(parentWorld)
               : Transpose(parentWorld) * desiredChildWorld;
}

namespace {

// The tracker's axes mapped onto cameraRoot's, still in metres. The single
// definition both lean paths derive from.
NiPoint3 TrackerAxesToCameraFrame(float metersX, float metersY, float metersZ) {
    return NiPoint3(metersX, -metersZ, metersY);
}

}  // namespace

NiPoint3 TrackerLeanToCameraLocalUnits(float metersX, float metersY, float metersZ) {
    // The core's z runs the other way from the engine's forward axis: NEGATIVE z
    // is the lean toward the screen, and cameraRoot's row 1 points forward. The
    // negation belongs here, at the one place the two conventions meet.
    //
    // It used to be done with InvertZ=true in the INI instead, and that is a
    // different thing wearing the same clothes: inversion is applied BEFORE the
    // clamp, so it swapped the limits over as well as the sign. Measured on this
    // build with InvertZ=true: a 25 cm lean IN came out as +0.10 m, pinned
    // against the 0.10 m backward limit, while a 25 cm lean BACK came out as the
    // full -0.25 m. Leaning in barely moved and pulling back moved a lot, which
    // is exactly the shape the doctrine warns this mistake takes.
    const NiPoint3 meters = TrackerAxesToCameraFrame(metersX, metersY, metersZ);
    return NiPoint3(meters.x * kUnitsPerMeter, meters.y * kUnitsPerMeter,
                    meters.z * kUnitsPerMeter);
}

NiPoint3 TrackerLeanToWorldUnits(const NiMatrix33& rootWorldRot,
                                 float metersX, float metersY, float metersZ) {
    // Scaled after the rotation, not before: the two orders differ in the last
    // bit or two of the float, and the characterization test that guards this
    // maths compares exactly.
    NiPoint3 world = rootWorldRot.LocalToWorld(
        TrackerAxesToCameraFrame(metersX, metersY, metersZ));
    world.x *= kUnitsPerMeter;
    world.y *= kUnitsPerMeter;
    world.z *= kUnitsPerMeter;
    return world;
}

AimProjection ProjectBodyAimToNdc(const float (&cleanNiCamWorld)[3][4],
                                  const float (&trackedNiCamWorld)[3][4],
                                  float frustumRight, float frustumTop) {
    const float* cleanForward   = cleanNiCamWorld[0];
    const float* trackedForward = trackedNiCamWorld[0];
    const float* trackedUp      = trackedNiCamWorld[1];
    const float* trackedRight   = trackedNiCamWorld[2];

    const float aimRight = cleanForward[0] * trackedRight[0]
                         + cleanForward[1] * trackedRight[1]
                         + cleanForward[2] * trackedRight[2];
    const float aimForward = cleanForward[0] * trackedForward[0]
                           + cleanForward[1] * trackedForward[1]
                           + cleanForward[2] * trackedForward[2];
    const float aimUp = cleanForward[0] * trackedUp[0]
                      + cleanForward[1] * trackedUp[1]
                      + cleanForward[2] * trackedUp[2];

    AimProjection out{0.0f, 0.0f, false};
    if (aimForward > kMinAimForwardCosine && frustumRight > 0.0f && frustumTop > 0.0f) {
        out.ndcX = (aimRight / aimForward) / frustumRight;
        out.ndcY = (aimUp / aimForward) / frustumTop;
        out.valid = true;
    }
    return out;
}

} // namespace Fallout4HT
