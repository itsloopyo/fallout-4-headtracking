// SPDX-License-Identifier: MIT

#pragma once

#include "game/fallout4_types.h"

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

// The lean in cameraRoot's own frame, in engine units: X right, Y forward,
// Z up, which is the order cameraRoot's rows are in and therefore the frame a
// CHILD node's local translation is expressed in.
//
// The mod writes the lean in two places - niCamera's world translation and its
// local translation - and the engine recomputes the world one from the local one
// within the frame, so the local write is what actually reaches the screen. They
// must derive the axis mapping from here rather than each spelling it out: when
// they disagreed, the world write looked right in every dump while the camera
// leaned the opposite way on screen.
NiPoint3 TrackerLeanToCameraLocalUnits(float metersX, float metersY, float metersZ);

// The same lean rotated into world space. Rows are the axes, so a camera-frame
// vector is recombined FROM the rows rather than multiplied through them.
NiPoint3 TrackerLeanToWorldUnits(const NiMatrix33& rootWorldRot,
                                 float metersX, float metersY, float metersZ);

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
