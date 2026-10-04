// SPDX-License-Identifier: MIT

#include "pch.h"
#include "motion_probe.h"
#include "core/path_utils.h"
#include "core/seh_guard.h"
#include "hooks/player_hook.h"

#include <cstdio>
#include <share.h>

namespace Fallout4HT {
namespace {

// TESObjectREFR position, and TESCameraState::id.
constexpr uintptr_t kRefPosition = 0xD0;
constexpr uintptr_t kCameraStateId = 0x20;

FILE* g_file = nullptr;
bool g_opened = false;

float Dot(const float* row, const NiPoint3& v) { return row[0] * v.x + row[1] * v.y + row[2] * v.z; }

bool ReadPlayer(void* camera, NiPoint3& position, uint32_t& stateId) {
    static std::atomic<uint64_t> s_faults{0};
    __try {
        const uintptr_t player = reinterpret_cast<uintptr_t>(PlayerActor());
        if (player == 0) return false;
        position = *reinterpret_cast<const NiPoint3*>(player + kRefPosition);
        const uintptr_t state = *reinterpret_cast<const uintptr_t*>(
            reinterpret_cast<uintptr_t>(camera) + TESCameraOffsets::CurrentState);
        stateId = state ? *reinterpret_cast<const uint32_t*>(state + kCameraStateId) : 0xFFFFFFFFu;
        return true;
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "motion probe", s_faults)) {
    }
    return false;
}

}  // namespace

void RecordMotionProbe(void* camera, const NiMatrix33& root, const NiPoint3& cleanEye, const NiPoint3& offset,
                       float leanScale, float leanX, float leanY, float leanZ, float yaw, float pitch, float roll) {
    if (!g_opened) {
        g_opened = true;
        g_file = _wfsopen((GetModuleDirectoryW() + L"CameraUnlockProbe.csv").c_str(), L"w", _SH_DENYNO);
        if (g_file) {
            fprintf(g_file, "ms,state,playerX,playerY,playerZ,eyeR,eyeF,eyeU,offR,offF,offU,scale,"
                            "leanX,leanY,leanZ,yaw,pitch,roll\n");
        }
    }
    if (!g_file) return;
    NiPoint3 player;
    uint32_t stateId = 0;
    if (!ReadPlayer(camera, player, stateId)) return;
    const NiPoint3 rel(cleanEye.x - player.x, cleanEye.y - player.y, cleanEye.z - player.z);
    fprintf(g_file, "%llu,%u,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.3f,%.4f,%.4f,%.4f,%.2f,%.2f,%.2f\n",
            static_cast<unsigned long long>(GetTickCount64()), stateId, player.x, player.y, player.z,
            Dot(root.entry[0], rel), Dot(root.entry[1], rel), Dot(root.entry[2], rel),
            Dot(root.entry[0], offset), Dot(root.entry[1], offset), Dot(root.entry[2], offset), leanScale,
            leanX, leanY, leanZ, yaw, pitch, roll);
    fflush(g_file);
}

}  // namespace Fallout4HT
