// SPDX-License-Identifier: MIT
//
// Characterization tests for the head-tracking maths lifted out of the camera
// hook. Every Ref* function below is the pre-extraction expression copied
// verbatim out of camera_hook.cpp, so these tests do not assert that the maths
// is *right* - the game already settled that - they assert that moving it did
// not change a single result. The comparisons are exact: the extracted code
// performs the same operations in the same order, so any drift at all is a
// regression rather than rounding.

#include "core/constants.h"
#include "core/vector_math.h"
#include "game/fallout4_types.h"
#include "hooks/camera_math.h"
#include "hooks/crosshair_layout.h"

#include <cmath>
#include <cstdio>

using namespace Fallout4HT;

namespace {

int g_failures = 0;

void Check(bool cond, const char* what) {
    if (!cond) {
        std::printf("  FAIL: %s\n", what);
        ++g_failures;
    }
}

bool RowsEqual(const NiMatrix33& a, const NiMatrix33& b) {
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 4; ++j)
            if (a.entry[i][j] != b.entry[i][j]) return false;
    return true;
}

bool RowsNear(const NiMatrix33& a, const NiMatrix33& b, float tolerance = 1e-5f) {
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            if (std::fabs(a.entry[i][j] - b.entry[i][j]) > tolerance) return false;
    return true;
}

// --- Pre-extraction reference implementations -------------------------------

void RefHeadRotation(float yaw, float pitch, float roll, bool worldYaw,
                     NiMatrix33& headLocal, NiMatrix33& worldYawRot) {
    const float yawRad = yaw * DEG_TO_RAD;
    const float pitchRad = pitch * DEG_TO_RAD;
    const float rollRad = roll * DEG_TO_RAD;
    headLocal = NiMatrix33::FromEulerAngles(worldYaw ? 0.0f : yawRad, -pitchRad, rollRad);
    worldYawRot = worldYaw ? NiMatrix33::FromEulerAngles(yawRad, 0.0f, 0.0f) : NiMatrix33();
}

NiPoint3 RefLean(const NiMatrix33& rootWorldRot, float posX, float posY, float posZ) {
    constexpr float UNITS_PER_METER = 70.0f;
    // Z is negated: the core's negative z is the lean toward the screen, and
    // cameraRoot's row 1 points forward. This used to be done with InvertZ=true
    // in the INI, which also swapped the asymmetric forward/back limits over and
    // left leaning in with 0.10 m of travel against 0.40 m for leaning back.
    const NiPoint3 localOffset(posX, -posZ, posY);
    NiPoint3 worldOffset = rootWorldRot.LocalToWorld(localOffset);
    worldOffset.x *= UNITS_PER_METER;
    worldOffset.y *= UNITS_PER_METER;
    worldOffset.z *= UNITS_PER_METER;
    return worldOffset;
}

void RefAim(const float (&clean)[3][4], const float (&tracked)[3][4],
            float frustumRight, float frustumTop,
            float& ndcX, float& ndcY, bool& valid) {
    const float* cf = clean[0];
    const float* tf = tracked[0];
    const float* tu = tracked[1];
    const float* tr = tracked[2];

    const float aimRight = cf[0] * tr[0] + cf[1] * tr[1] + cf[2] * tr[2];
    const float aimFwd = cf[0] * tf[0] + cf[1] * tf[1] + cf[2] * tf[2];
    const float aimUp = cf[0] * tu[0] + cf[1] * tu[1] + cf[2] * tu[2];

    ndcX = 0.0f;
    ndcY = 0.0f;
    valid = false;
    if (aimFwd > 0.01f && frustumRight > 0.0f && frustumTop > 0.0f) {
        ndcX = (aimRight / aimFwd) / frustumRight;
        ndcY = (aimUp / aimFwd) / frustumTop;
        valid = true;
    }
}

// The clamped-acos and distance expressions as they were open-coded in
// camera_hook.cpp, player_hook.cpp, view_matrix_hook.cpp, frame_verdict.cpp and
// aim_decoupling.cpp before they moved into core/vector_math.h.
float RefAngleDeg(const float* a, const float* b) {
    float dot = 0.0f;
    for (int i = 0; i < 3; ++i) dot += a[i] * b[i];
    dot = dot > 1.0f ? 1.0f : (dot < -1.0f ? -1.0f : dot);
    return acosf(dot) * 57.2957795f;
}

float RefDistance(const float* a, const float* b) {
    float d2 = 0.0f;
    for (int i = 0; i < 3; ++i) {
        const float d = a[i] - b[i];
        d2 += d * d;
    }
    return sqrtf(d2);
}

void RefStageOffset(bool haveSnap, bool aimValid, float ndcX, float ndcY,
                    float frustumRight, float frustumTop, double& dx, double& dy) {
    constexpr double kStageHalfHeight = 360.0;
    constexpr double kStageHalfWidth = 640.0;
    constexpr double kOffScreen = 10000.0;

    dx = 0.0;
    dy = 0.0;
    if (!haveSnap) {
        // Back to where the game authored it.
    } else if (!aimValid) {
        dx = kOffScreen;
    } else {
        const double aspect = static_cast<double>(frustumRight) / frustumTop;
        const double halfWidth = kStageHalfHeight * aspect;
        dx = static_cast<double>(ndcX)
             * (halfWidth > kStageHalfWidth ? halfWidth : kStageHalfWidth);
        dy = -static_cast<double>(ndcY) * kStageHalfHeight;
    }
}

// --- Fixtures ---------------------------------------------------------------

// A plausible pose sweep: zero, single-axis, and combined poses either side of
// centre, plus angles past where a head realistically goes.
const float kAngles[] = { 0.0f, 1.0f, -1.0f, 12.5f, -12.5f, 30.0f, -30.0f, 89.0f, -170.0f };

// niCamera.local is a row permutation of cameraRoot's basis, so an arbitrary
// rotation stands in for either node's basis well enough to exercise the
// composition and the projection.
NiMatrix33 SampleBasis(float yawDeg, float pitchDeg, float rollDeg) {
    return NiMatrix33::FromEulerAngles(yawDeg * DEG_TO_RAD, pitchDeg * DEG_TO_RAD,
                                       rollDeg * DEG_TO_RAD);
}

void CopyRows(const NiMatrix33& from, float (&to)[3][4]) {
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 4; ++j)
            to[i][j] = from.entry[i][j];
}

// --- Tests ------------------------------------------------------------------

void VectorHelpersMatchPreExtraction() {
    std::printf("core/vector_math.h matches the pre-extraction expressions\n");

    // Rows of rotation matrices, which is what every caller feeds these: unit
    // length by construction, and routinely nearly parallel, which is where an
    // unclamped acos turns into NaN.
    bool angleMatches = true;
    bool distanceMatches = true;
    for (float yaw : kAngles) {
        for (float pitch : kAngles) {
            const NiMatrix33 a = SampleBasis(yaw, pitch, 3.0f);
            const NiMatrix33 b = SampleBasis(pitch, yaw, -7.0f);
            for (int row = 0; row < 3; ++row) {
                if (DegreesBetweenUnit(a.entry[row], b.entry[row])
                        != RefAngleDeg(a.entry[row], b.entry[row])) {
                    angleMatches = false;
                }
                if (Distance3(a.entry[row], b.entry[row])
                        != RefDistance(a.entry[row], b.entry[row])) {
                    distanceMatches = false;
                }
            }
        }
    }
    Check(angleMatches, "DegreesBetweenUnit is unchanged across the basis sweep");
    Check(distanceMatches, "Distance3 is unchanged across the basis sweep");

    // A vector against itself is the case that produced NaN before the clamp: the
    // dot rounds a shade past 1.0 and acos of that is not a number.
    const NiMatrix33 basis = SampleBasis(37.0f, -19.0f, 11.0f);
    for (int row = 0; row < 3; ++row) {
        Check(!std::isnan(DegreesBetweenUnit(basis.entry[row], basis.entry[row])),
              "a row against itself is a number, not NaN");
        Check(DegreesBetweenUnit(basis.entry[row], basis.entry[row]) == 0.0f,
              "a row against itself is zero degrees");
        Check(Distance3(basis.entry[row], basis.entry[row]) == 0.0f,
              "a row against itself is zero distance");
    }

    Check(ClampToUnitRange(1.5f) == 1.0f && ClampToUnitRange(-1.5f) == -1.0f
              && ClampToUnitRange(0.25f) == 0.25f, "ClampToUnitRange bounds without disturbing the middle");

    // Antiparallel rows are the other end of the clamp, and 180 degrees is what
    // the callers rely on to classify a view that flipped.
    const float forward[3] = { 0.0f, 1.0f, 0.0f };
    const float backward[3] = { 0.0f, -1.0f, 0.0f };
    Check(std::fabs(DegreesBetweenUnit(forward, backward) - 180.0f) < 1e-3f,
          "opposed unit vectors read 180 degrees");
    Check(Distance3(forward, backward) == 2.0f, "opposed unit vectors are 2 apart");

    // RAD_TO_DEG is the literal the call sites carried, not 1/DEG_TO_RAD - the
    // two differ, and the naming must not have quietly swapped one for the other.
    Check(RAD_TO_DEG == 57.2957795f, "RAD_TO_DEG is the constant the call sites used");
}

void MatrixInvariants() {
    std::printf("NiMatrix33 invariants\n");

    const NiMatrix33 identity;
    Check(identity.entry[0][0] == 1.0f && identity.entry[1][1] == 1.0f
              && identity.entry[2][2] == 1.0f, "default construction is identity");
    Check(identity.entry[0][3] == 0.0f && identity.entry[1][3] == 0.0f
              && identity.entry[2][3] == 0.0f, "identity zeroes the pad column");

    Check(RowsEqual(NiMatrix33::FromEulerAngles(0.0f, 0.0f, 0.0f), identity),
          "a zero rotation is the identity");

    const NiMatrix33 a = SampleBasis(23.0f, -41.0f, 17.0f);
    const NiMatrix33 product = a * SampleBasis(-9.0f, 55.0f, -3.0f);
    Check(product.entry[0][3] == 0.0f && product.entry[1][3] == 0.0f
              && product.entry[2][3] == 0.0f, "multiplication keeps the pad column zero");

    const NiMatrix33 roundTrip = Transpose(Transpose(a));
    Check(RowsEqual(roundTrip, a), "transposing twice restores the matrix");

    // A rotation matrix times its own transpose is the identity, which is the
    // property the conjugation in the hook relies on.
    const NiMatrix33 shouldBeIdentity = a * Transpose(a);
    bool orthonormal = true;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            if (std::fabs(shouldBeIdentity.entry[i][j] - identity.entry[i][j]) > 1e-5f)
                orthonormal = false;
    Check(orthonormal, "a rotation times its transpose is the identity");
}

void HeadRotationMatchesPreExtraction() {
    std::printf("ComputeHeadRotation matches the pre-extraction composition\n");

    bool cameraFrameMatches = true;
    bool worldFrameMatches = true;
    for (float yaw : kAngles) {
        for (float pitch : kAngles) {
            for (float roll : kAngles) {
                for (int mode = 0; mode < 2; ++mode) {
                    const bool worldYaw = (mode == 1);
                    NiMatrix33 refLocal;
                    NiMatrix33 refWorld;
                    RefHeadRotation(yaw, pitch, roll, worldYaw, refLocal, refWorld);

                    const HeadRotation got = ComputeHeadRotation(yaw, pitch, roll, worldYaw);
                    if (!RowsEqual(got.cameraFrame, refLocal)) cameraFrameMatches = false;
                    if (!RowsEqual(got.worldFrame, refWorld)) worldFrameMatches = false;
                }
            }
        }
    }
    Check(cameraFrameMatches, "camera-frame rotation is unchanged across the pose sweep");
    Check(worldFrameMatches, "world-frame rotation is unchanged across the pose sweep");

    // Horizon locking is what moves yaw between the two frames; the split itself
    // is the behaviour worth pinning.
    const HeadRotation local = ComputeHeadRotation(30.0f, 0.0f, 0.0f, false);
    const HeadRotation world = ComputeHeadRotation(30.0f, 0.0f, 0.0f, true);
    Check(RowsEqual(world.worldFrame, local.cameraFrame),
          "world-yaw mode moves the same yaw rotation into the world frame");
    Check(RowsEqual(local.worldFrame, NiMatrix33()),
          "local-yaw mode leaves the world frame at identity");
    Check(RowsEqual(world.cameraFrame, NiMatrix33()),
          "world-yaw mode leaves pure yaw out of the camera frame");
}

void ChildLocalSolveRoundTrips() {
    std::printf("SolveChildLocal round trips both engine composition orders\n");

    const NiMatrix33 parent = SampleBasis(31.0f, -12.0f, 7.0f);
    const NiMatrix33 local = SampleBasis(-8.0f, 19.0f, 4.0f);
    const NiMatrix33 desired = SampleBasis(47.0f, -21.0f, 13.0f);

    const NiMatrix33 leftWorld = local * parent;
    const NiMatrix33 leftSolved = SolveChildLocal(
        desired, parent, local, leftWorld, parent);
    Check(RowsNear(leftSolved * parent, desired),
          "left-multiplied child local rebuilds the desired world rotation");

    const NiMatrix33 rightWorld = parent * local;
    const NiMatrix33 rightSolved = SolveChildLocal(
        desired, parent, local, rightWorld, parent);
    Check(RowsNear(parent * rightSolved, desired),
          "right-multiplied child local rebuilds the desired world rotation");
}

void LeanMatchesPreExtraction() {
    std::printf("TrackerLeanToWorldUnits matches the pre-extraction offset\n");

    const float kOffsets[] = { 0.0f, 0.05f, -0.05f, 0.3f, -0.1f };
    bool matches = true;
    for (float x : kOffsets) {
        for (float y : kOffsets) {
            for (float z : kOffsets) {
                const NiMatrix33 basis = SampleBasis(41.0f, -13.0f, 6.0f);
                const NiPoint3 got = TrackerLeanToWorldUnits(basis, x, y, z);
                const NiPoint3 want = RefLean(basis, x, y, z);
                if (got.x != want.x || got.y != want.y || got.z != want.z) matches = false;
            }
        }
    }
    Check(matches, "lean offset is unchanged across the offset sweep");

    // A metre of tracker movement is 70 engine units, and with an identity basis
    // the tracker's Y (up) lands on the engine's Z (up).
    const NiPoint3 up = TrackerLeanToWorldUnits(NiMatrix33(), 0.0f, 1.0f, 0.0f);
    Check(up.x == 0.0f && up.y == 0.0f && up.z == kUnitsPerMeter,
          "one metre up is kUnitsPerMeter on the engine up axis");

    // The core clamps z to [-limit_z, +limit_z_back] - the generous forward
    // limit sits on NEGATIVE z - so a negative z has to come out along the
    // camera's forward axis. Getting this backwards is survivable at a glance,
    // because the camera does still move along the right axis; only the travel
    // is wrong, and the mod then lets the player lean in by 0.10 m and back by
    // 0.40 m. Row 1 of the basis is forward.
    const NiPoint3 forwardLean = TrackerLeanToWorldUnits(NiMatrix33(), 0.0f, 0.0f, -1.0f);
    Check(forwardLean.x == 0.0f && forwardLean.y == kUnitsPerMeter && forwardLean.z == 0.0f,
          "the core's negative z leans the camera FORWARD, where the 0.40 m limit is");

    // The mod writes the lean twice - once rotated into world space, once
    // unrotated into the child's local translation - and the engine rebuilds the
    // first from the second inside the frame, so only the local one reaches the
    // screen. They disagreed about the sign of the forward axis for a while and
    // nothing that read the world value could see it.
    bool agree = true;
    for (float x : kOffsets) {
        for (float y : kOffsets) {
            for (float z : kOffsets) {
                const NiPoint3 local = TrackerLeanToCameraLocalUnits(x, y, z);
                const NiPoint3 world = TrackerLeanToWorldUnits(NiMatrix33(), x, y, z);
                if (local.x != world.x || local.y != world.y || local.z != world.z) agree = false;
            }
        }
    }
    Check(agree, "the local and world lean agree axis for axis under an identity basis");
}

void AimProjectionMatchesPreExtraction() {
    std::printf("ProjectBodyAimToNdc matches the pre-extraction projection\n");

    const float kFrustums[][2] = { { 1.61781f, 0.45501f }, { 1.0f, 0.5625f },
                                   { 0.0f, 0.45501f }, { 1.61781f, 0.0f } };
    bool matches = true;
    for (float yaw : kAngles) {
        for (float pitch : kAngles) {
            float clean[3][4];
            float tracked[3][4];
            CopyRows(SampleBasis(0.0f, 0.0f, 0.0f), clean);
            CopyRows(SampleBasis(yaw, pitch, 9.0f), tracked);

            for (const auto& frustum : kFrustums) {
                float refX = 0.0f;
                float refY = 0.0f;
                bool refValid = false;
                RefAim(clean, tracked, frustum[0], frustum[1], refX, refY, refValid);

                const AimProjection got =
                    ProjectBodyAimToNdc(clean, tracked, frustum[0], frustum[1]);
                if (got.valid != refValid || got.ndcX != refX || got.ndcY != refY) {
                    matches = false;
                }
            }
        }
    }
    Check(matches, "aim projection is unchanged across the pose and frustum sweep");

    // Head and body pointing the same way puts the reticle dead centre.
    float sameClean[3][4];
    float sameTracked[3][4];
    CopyRows(SampleBasis(37.0f, -8.0f, 4.0f), sameClean);
    CopyRows(SampleBasis(37.0f, -8.0f, 4.0f), sameTracked);
    const AimProjection centred = ProjectBodyAimToNdc(sameClean, sameTracked, 1.61781f, 0.45501f);
    Check(centred.valid, "an untracked head projects a valid aim point");
    Check(std::fabs(centred.ndcX) < 1e-5f && std::fabs(centred.ndcY) < 1e-5f,
          "an untracked head puts the aim at screen centre");

    // Turned far enough that body aim leaves the view, the projection has to
    // report invalid rather than claim the shot goes to the middle of the
    // screen.
    float behindTracked[3][4];
    CopyRows(SampleBasis(179.0f, 0.0f, 0.0f), behindTracked);
    const AimProjection behind = ProjectBodyAimToNdc(sameClean, behindTracked, 1.61781f, 0.45501f);
    Check(!behind.valid, "aim behind the tracked view is reported invalid");
}

void CrosshairStageOffsetMatchesPreExtraction() {
    std::printf("ComputeCrosshairStageOffset matches the pre-extraction mapping\n");

    const float kNdc[] = { 0.0f, 0.25f, -0.25f, 0.9f, -1.4f };
    // 4:3, 16:9 (degenerate - every model agrees), 21:9 and 32:9.
    const float kFrustums[][2] = { { 0.6f, 0.45f }, { 1.0f, 0.5625f },
                                   { 1.05f, 0.45f }, { 1.61781f, 0.45501f } };

    bool matches = true;
    for (int haveSnap = 0; haveSnap < 2; ++haveSnap) {
        for (int aimValid = 0; aimValid < 2; ++aimValid) {
            for (float ndcX : kNdc) {
                for (float ndcY : kNdc) {
                    for (const auto& frustum : kFrustums) {
                        double wantDx = 0.0;
                        double wantDy = 0.0;
                        RefStageOffset(haveSnap != 0, aimValid != 0, ndcX, ndcY,
                                       frustum[0], frustum[1], wantDx, wantDy);

                        const CrosshairStageOffset got = ComputeCrosshairStageOffset(
                            haveSnap != 0, aimValid != 0, ndcX, ndcY, frustum[0], frustum[1]);
                        if (got.dx != wantDx || got.dy != wantDy) matches = false;
                    }
                }
            }
        }
    }
    Check(matches, "stage offset is unchanged across the aspect and NDC sweep");

    // Below 16:9 the horizontal half-stage stays pinned at 640, above it grows
    // with the aspect. Both branches matter - this is the bug that shipped once.
    const CrosshairStageOffset narrow =
        ComputeCrosshairStageOffset(true, true, 1.0f, 0.0f, 0.6f, 0.45f);
    Check(narrow.dx == 640.0, "a 4:3 viewport uses the flat 640 half-stage");
    const CrosshairStageOffset wide =
        ComputeCrosshairStageOffset(true, true, 1.0f, 0.0f, 1.61781f, 0.45501f);
    Check(wide.dx > 640.0, "a 32:9 viewport widens the half-stage past 640");

    // Scaleform's Y points down, so an aim above centre needs a negative dy.
    const CrosshairStageOffset above =
        ComputeCrosshairStageOffset(true, true, 0.0f, 0.5f, 1.0f, 0.5625f);
    Check(above.dy == -180.0, "an aim above centre maps to a negative stage Y");

    const CrosshairStageOffset noAim =
        ComputeCrosshairStageOffset(false, true, 0.9f, 0.9f, 1.0f, 0.5625f);
    Check(noAim.dx == 0.0 && noAim.dy == 0.0,
          "no snapshot restores the authored position");

    const CrosshairStageOffset hidden =
        ComputeCrosshairStageOffset(true, false, 0.0f, 0.0f, 1.0f, 0.5625f);
    Check(hidden.dx == 10000.0, "an invalid aim parks the reticle off-screen");
}

}  // namespace

int main() {
    std::printf("Fallout4HeadTracking camera math tests\n"
                "======================================\n");
    VectorHelpersMatchPreExtraction();
    MatrixInvariants();
    HeadRotationMatchesPreExtraction();
    ChildLocalSolveRoundTrips();
    LeanMatchesPreExtraction();
    AimProjectionMatchesPreExtraction();
    CrosshairStageOffsetMatchesPreExtraction();

    if (g_failures == 0) {
        std::printf("All tests passed!\n");
        return 0;
    }
    std::printf("%d test(s) FAILED\n", g_failures);
    return 1;
}
