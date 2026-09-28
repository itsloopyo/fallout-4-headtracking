// SPDX-License-Identifier: MIT
#pragma once

#include <cameraunlock/camera/lean_clamp.h>
#include "game/fallout4_types.h"

namespace Fallout4HT::lean_trace {
void Initialize();
void Reset();
float Clamp(const NiPoint3& eye, const NiPoint3& offset, uintptr_t camera,
            uintptr_t state, float nearPlane, uint64_t tick, float deltaTime);
cameraunlock::camera::LeanObstruction Query(void* context,
    const cameraunlock::math::Vec3& start, const cameraunlock::math::Vec3& direction,
    float maxDistance);
}
