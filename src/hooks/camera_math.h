// SPDX-License-Identifier: MIT

#pragma once

#include "game/fallout4_types.h"

#include <cameraunlock/ads/lean_handover.h>
#include <cameraunlock/math/vec3.h>

namespace Fallout4HT {

// Pure head-tracking maths, free of engine pointers and Windows APIs so it can
// be exercised outside the game. Every function here derives from the row-vector
// convention documented in fallout4_types.h: the ROWS of an NiAVObject world
// rotation are the node's world-space axes.

// The head rotation split into the two frames it is applied in. The camera-frame
// part PRE-multiplies (it recombines the rows, so it rotates in the camera's own
// frame); the world-frame part POST-multiplies (sending each axis row v to v*P,
// which is the world rotation P transpose).
struct HeadRotation {
    NiMatrix33 cameraFrame;
    NiMatrix33 worldFrame;
};

// Signs were settled in game against a live tracker: positive yaw turns the view
// right, positive pitch looks up, positive roll tilts the view with the head.
// Pitch is the one axis whose sign the pre-multiply gets backwards on its own,
// hence the lone negation.
//
// With horizon locking on (worldSpaceYaw), yaw moves out of the camera frame and
// into a rotation about world Z; post-multiplying by Rz(+yaw) applies world
// Rz(-yaw), which turns the view right.
HeadRotation ComputeHeadRotation(float yawDeg, float pitchDeg, float rollDeg,
                                 bool worldSpaceYaw);

NiMatrix33 Transpose(const NiMatrix33& m);

// Rebuild a child node's world rotation from its parent's, through whichever
// composition the engine is actually using.
//
// niCamera.world and cameraRoot.world are not independent: the engine derives one
// from the other and rebuilds it whenever the scene graph updates. Writing the
// two separately - each from its own Euler composition - leaves them holding
// rotations that disagree, and the next rebuild then replaces the view with a
// pose this mod never asked for: one un-tracked frame, every time an animation
// or a cell transition triggers a rebuild. The same mistake in the Skyrim SE
// port produced exactly that ("any scene-graph rebuild of niCamera.world snapped
// the view back un-tracked"), and the fix there was to derive one from the other
// rather than build both.
//
// Which side the child's local basis multiplies on is MEASURED from the observed
// clean pair each tick rather than assumed - the two ports of this code disagree
// about it, and being wrong is indistinguishable from being right until the
// engine rebuilds.
NiMatrix33 ComposeChildWorld(const NiMatrix33& childLocal,
                             const NiMatrix33& parentWorld,
                             const NiMatrix33& observedChildWorld,
                             const NiMatrix33& observedParentWorld);

// Finds the child-local rotation that produces desiredChildWorld from
// parentWorld, using the observed clean transforms to select the engine's
// multiplication order.
NiMatrix33 SolveChildLocal(const NiMatrix33& desiredChildWorld,
                           const NiMatrix33& parentWorld,
                           const NiMatrix33& observedChildLocal,
                           const NiMatrix33& observedChildWorld,
                           const NiMatrix33& observedParentWorld);

// A tracker lean, in metres, as a world-space offset in engine units. Rows are the
// axes, so a camera-frame vector is recombined FROM the rows rather than
// multiplied through them. A child node's local translation takes this back into
// cameraRoot's frame with WorldToLocal, so the two can never disagree about an
// axis.
NiPoint3 TrackerLeanToWorldUnits(const NiMatrix33& rootWorldRot,
                                 float metersX, float metersY, float metersZ);

// The clean camera's forward axis in tracker axes, the frame the lean is split in:
// a negative z lean moves the eye forward.
inline const cameraunlock::math::Vec3 kTrackerForward(0.0f, 0.0f, -1.0f);

// The lean's split between the camera and the first-person skeleton for this frame.
// Sights locked is a first-person mode: outside the first-person camera the camera
// carries the whole lean, sights up or not, and the handover starts again at the hip
// when the view comes back to first person.
cameraunlock::ads::LeanShares ShareLean(cameraunlock::ads::LeanHandover& handover, const cameraunlock::math::Vec3& lean,
                                        bool firstPerson, bool aiming, bool trueFreeLook, bool rigAvailable,
                                        unsigned long long nowMs);

// What the camera adds to a clean eye that already carries `rigWorld`, the rig's
// share of the lean, so the eye lands on the un-leaned eye plus `scale` of the
// whole lean however it is split between the two carriers. Negative where the
// collision clamp has tightened below what the rig carried.
NiPoint3 CameraShareOfLean(const NiPoint3& wholeLean, const NiPoint3& rigWorld, float scale);

// The last offset written to the first-person skeleton root, and the root's local
// translation right after the write.
struct RigWrite {
    uintptr_t rig = 0;
    NiPoint3 applied;
    NiPoint3 localAfter;
};

// What to add to the root so it carries `rigWorld` this tick. The engine rewrites
// the root from the player once a frame, but the camera can tick more than once in
// a frame, and a tick that finds the root still holding the last write replaces
// that write rather than adding to it.
NiPoint3 RigDelta(const RigWrite& last, uintptr_t rig, const NiPoint3& local, const NiPoint3& rigWorld);

// Where the body's aim direction lands in the head-tracked view.
struct AimProjection {
    float ndcX;
    float ndcY;
    bool valid;
};

// Rows are the world-space axes, so the clean forward row IS the aim direction,
// and its components in the tracked frame come from dotting against the tracked
// rows. Both bases are deliberately niCamera's rather than cameraRoot's, even
// though the two differ only by the row permutation L today: the frustum divided
// through below belongs to niCamera, so taking the aim, the basis and the
// frustum from one node means anything L ever picks up beyond that permutation
// (a shake node, a scope offset) cancels instead of leaking into the reticle
// position.
//
// niCamera row order is forward, up, right.
AimProjection ProjectBodyAimToNdc(const float (&cleanNiCamWorld)[3][4],
                                  const float (&trackedNiCamWorld)[3][4],
                                  float frustumRight, float frustumTop);

} // namespace Fallout4HT
