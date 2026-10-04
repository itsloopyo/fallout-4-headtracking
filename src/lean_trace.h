// SPDX-License-Identifier: MIT
#pragma once

#include <cameraunlock/camera/lean_clamp.h>
#include "game/fallout4_types.h"

namespace Fallout4HT::lean_trace {
void Initialize();
void Reset();

float Clamp(const NiPoint3& eye, const NiPoint3& offset, uintptr_t camera,
            uintptr_t state, float nearPlane, uint64_t tick, float deltaTime);
// What a ray from `start` along the unit `direction` first meets that a round
// would, within `range` units. `queried` is false when the query could not run,
// which is not the same answer as nothing in range.
struct AimRay {
    bool queried;
    bool hit;
    float distance;
    uint32_t filter;
};
AimRay CastAimRay(const NiPoint3& start, const NiPoint3& direction, float range);
cameraunlock::camera::LeanObstruction Query(void* context,
    const cameraunlock::math::Vec3& start, const cameraunlock::math::Vec3& direction,
    float maxDistance);
}
