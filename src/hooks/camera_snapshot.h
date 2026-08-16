// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

namespace Fallout4HT {

// Body-aimed engine state plus the tracked state computed for diagnostics and
// reticle projection. The tracked matrices are not left in the scene graph.
struct CameraRootSnapshots {
    uintptr_t cameraRoot;        // NiNode*; 0 if no frame processed yet
    uintptr_t niCamera;          // NiCamera*; 0 if not captured
    // Each rotation matrix is 48 bytes (NiMatrix33 - FO4 pads rows to 4 floats).
    // Copied by value, pad column included, so a restore writes back exactly the
    // bytes the engine had.
    float cleanWorld[3][4];
    float trackedWorld[3][4];
    // cameraRoot's local rotation in both states.
    float cleanLocal[3][4];
    float trackedLocal[3][4];
    // niCamera world rotation in both states.
    float cleanNiCamWorld[3][4];
    float trackedNiCamWorld[3][4];
    // worldToCam (the renderer's view-projection matrix) is rebuilt by the
    // engine from cameraRoot after the camera hook runs, so head tracking never
    // writes it. Kept as a diagnostic: its rows are the camera axes scaled by
    // the frustum extents, which is what pins the row-vector convention.
    // Valid iff niCamera != 0.
    float cleanWorldToCam[4][4];
    // NiCamera view-frustum extents, normalised to near=1: frustumRight =
    // tan(HFOV/2), frustumTop = tan(VFOV/2). Lets the crosshair project
    // body-aim to screen using the live FOV instead of guessing it.
    float frustumRight;
    float frustumTop;

    // Where the body's aim direction lands in the head-tracked view, in NDC
    // (-1..1, +x right, +y up). This is where the crosshair belongs once shots
    // no longer follow the head. aimValid is false when the aim is behind the
    // tracked view, which happens at extreme head yaw.
    float aimNdcX;
    float aimNdcY;
    bool aimValid;
};

// Publish this frame's snapshot. SINGLE WRITER: only the PlayerCamera::Update
// hook may call this, and only from the game's camera thread. A zeroed snapshot
// (cameraRoot == 0) is the "nothing usable this frame" marker.
void PublishCameraRootSnapshots(const CameraRootSnapshots& snapshots);

// Returns false if the last camera tick produced nothing usable - no frame
// processed yet, tracking off, in a menu, or the scene graph mid-rebuild. Safe
// to call from any thread.
bool GetCameraRootSnapshots(CameraRootSnapshots& out);

} // namespace Fallout4HT
