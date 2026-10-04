// SPDX-License-Identifier: MIT

#pragma once

#include "game/fallout4_types.h"

namespace Fallout4HT {

// Dev builds only: one CSV row per camera tick in CameraUnlockProbe.csv beside
// the mod, so a view that jerks can be read as numbers: where the game put the
// eye relative to the player, what the mod added, and what the collision clamp
// allowed, all in the clean camera's own right / forward / up axes.
void RecordMotionProbe(void* camera, const NiMatrix33& cleanRootWorld, const NiPoint3& cleanEye,
                       const NiPoint3& appliedOffset, float leanScale, float leanX, float leanY, float leanZ,
                       float yaw, float pitch, float roll);

// Dev builds only: one row per aim ray in CameraUnlockAim.csv, so a crosshair that
// jumps can be told apart: a ray that could not run, a ray that alternates between
// two depths with the aim still, or an aim that crossed an edge.
void RecordAimProbe(bool queried, bool hit, float distance, uint32_t filter, float ndcX, float ndcY,
                    float directionX, float directionY, const NiPoint3& aim);

} // namespace Fallout4HT
