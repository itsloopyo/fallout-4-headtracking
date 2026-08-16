// SPDX-License-Identifier: MIT

#pragma once

#include "camera_math.h"

namespace Fallout4HT {

struct RenderPose {
    HeadRotation rotation;
    float positionX;
    float positionY;
    float positionZ;
    bool hasPosition;
};

// Returns the pose published before the current PlayerCamera update. Every
// player view-matrix build consumes this pose while the engine nodes are locked.
bool LatestRenderPose(RenderPose& pose);
// Discover the PlayerCamera vtable via RTTI and hook its Update slot (index 3 -
// FO4 inserted a virtual SetEnabled at 2 that Skyrim lacks), then install the
// aim-decoupling and crosshair hooks that go with it. Synchronous.
//
// Returns false if RTTI discovery fails or aim decoupling cannot be installed;
// in either case nothing of ours is left hooked.
bool InstallCameraHook();

// Disable and unregister every hook InstallCameraHook created. Idempotent.
void RemoveCameraHook();

} // namespace Fallout4HT
