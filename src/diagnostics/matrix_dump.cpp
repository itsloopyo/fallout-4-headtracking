// SPDX-License-Identifier: MIT

#include "pch.h"
#include "matrix_dump.h"
#include "core/logging.h"
#include "diagnostics/frame_verdict.h"
#include "core/mod.h"
#include "core/seh_guard.h"
#include "game/fallout4_types.h"
#include "hooks/camera_snapshot.h"

namespace Fallout4HT {
namespace {

// The live node state the snapshot only names by address.
struct LiveNodeState {
    NiPoint3 cameraRootTranslate;
    NiPoint3 niCameraTranslate;
    NiMatrix33 niCameraLocal;
};

// The dump runs on the hotkey thread, so the nodes the snapshot names can be
// freed underneath it: the camera hook stops publishing when the player quits to
// the main menu, but the last snapshot it published stays readable. Copy the
// live reads out under a guard and log from the copies.
bool ReadLiveNodeState(const CameraRootSnapshots& snap, LiveNodeState& out) {
    static std::atomic<uint64_t> s_faults{0};
    __try {
        out.cameraRootTranslate = *WorldTranslationOf(snap.cameraRoot);
        out.niCameraTranslate = *WorldTranslationOf(snap.niCamera);
        out.niCameraLocal = *LocalRotationOf(snap.niCamera);
        return true;
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "matrix dump", s_faults)) {
    }
    return false;
}

void LogRotationRows(const char* label, const float (&rows)[3][4]) {
    Log::Line("%s:", label);
    for (int i = 0; i < 3; ++i)
        Log::Line("  [%+.4f %+.4f %+.4f]", rows[i][0], rows[i][1], rows[i][2]);
}

} // namespace

void DumpCameraMatrices() {
    CameraRootSnapshots snap;
    if (!GetCameraRootSnapshots(snap) || snap.niCamera == 0) {
        Log::Line("matrix dump: no snapshot available");
        return;
    }

    LiveNodeState live{};
    const bool liveReadable = ReadLiveNodeState(snap, live);

    {
        // Whether the game is actually running, asked on demand. Everything
        // below is a snapshot that outlives the tick that made it, so it reads
        // exactly the same in VATS, in the Pip-Boy and at the pause menu as it
        // does in gameplay - and a keystroke sent in those states means
        // something completely different from what was intended.
        unsigned long long ticks = 0;
        unsigned long long sinceMs = 0;
        GetCameraTickLiveness(ticks, sinceMs);
        Log::Line("=== MATRIX DUMP === camera ticks %llu, last one %llu ms ago (%s)",
                  ticks, sinceMs,
                  sinceMs < 250 ? "GAMEPLAY, the game is running"
                                : "PAUSED: VATS, a menu, or not in game");
    }
    Log::Line("cameraRoot=0x%llX niCamera=0x%llX",
        static_cast<unsigned long long>(snap.cameraRoot),
        static_cast<unsigned long long>(snap.niCamera));
    if (!liveReadable) {
        Log::Line("(nodes no longer readable - snapshot values only)");
    } else {
        Log::Line("worldTranslate cameraRoot=[%+.2f %+.2f %+.2f] niCamera=[%+.2f %+.2f %+.2f]",
            live.cameraRootTranslate.x, live.cameraRootTranslate.y, live.cameraRootTranslate.z,
            live.niCameraTranslate.x, live.niCameraTranslate.y, live.niCameraTranslate.z);
        LogRotationRows("niCamera.local (basis change)", live.niCameraLocal.entry);
    }

    // Read-only accessors, not the pipeline-advancing ones: this runs on the
    // hotkey thread, and driving the session from here would race the render
    // thread's per-frame Update.
    const Mod& mod = Mod::Instance();
    float px, py, pz;
    const bool hasPos = mod.GetLastPositionOffset(px, py, pz);
    Log::Line("PositionOffset: %s [%+.4f %+.4f %+.4f] m",
        hasPos ? "valid" : "INVALID", px, py, pz);

    LogRotationRows("cleanWorld (cameraRoot)", snap.cleanWorld);
    LogRotationRows("trackedWorld (cameraRoot)", snap.trackedWorld);
    LogRotationRows("cleanNiCamWorld", snap.cleanNiCamWorld);
    LogRotationRows("trackedNiCamWorld", snap.trackedNiCamWorld);

    Log::Line("cleanWorldToCam (last row should read ~[0 0 0 1] if WorldToCam offset is right):");
    for (int i = 0; i < 4; ++i)
        Log::Line("  [%+.4f %+.4f %+.4f %+.4f]",
            snap.cleanWorldToCam[i][0], snap.cleanWorldToCam[i][1],
            snap.cleanWorldToCam[i][2], snap.cleanWorldToCam[i][3]);
    Log::Line("frustumRight=%+.4f frustumTop=%+.4f (should be symmetric, ~aspect ratio apart)",
        snap.frustumRight, snap.frustumTop);
    Log::Line("aim: %s ndc=[%+.5f %+.5f]",
        snap.aimValid ? "valid" : "BEHIND VIEW", snap.aimNdcX, snap.aimNdcY);

    float yaw, pitch, roll;
    if (mod.GetLastRotation(yaw, pitch, roll)) {
        Log::Line("OpenTrack yaw=%+.3f pitch=%+.3f roll=%+.3f (deg)", yaw, pitch, roll);
    }
    Log::Line("YawMode: %s", mod.IsWorldSpaceYaw() ? "WORLD" : "LOCAL");
    Log::Line("=== END MATRIX DUMP ===");
}

} // namespace Fallout4HT
