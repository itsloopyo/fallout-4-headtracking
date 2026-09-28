// SPDX-License-Identifier: MIT
#include "hooks/collision_math.h"
#include <cameraunlock/camera/lean_clamp.h>
#include <cstdio>

using namespace Fallout4HT;
using cameraunlock::math::Vec3;
using cameraunlock::camera::LeanObstruction;

struct Wall {
    float distance;
    float cosine;
    bool available = true;
    float traced = 0;
};

LeanObstruction Trace(void* context, const Vec3&, const Vec3&, float maxDistance) {
    auto& wall = *static_cast<Wall*>(context);
    wall.traced = CollisionTraceRange(maxDistance, 10);
    if (!wall.available) return {};
    return {true, wall.distance <= wall.traced,
        CollisionHitAllowance(wall.distance, 10, wall.cosine)};
}

int main() {
    int failed = 0;
    const auto check = [&](bool condition, const char* name) {
        if (!condition) { std::printf("FAIL: %s\n", name); ++failed; }
    };
    cameraunlock::camera::LeanClamp clamp;
    clamp.SetSettings({10, 0.9f});
    Wall wall{25, -1};
    auto offset = clamp.Apply({}, {30,0,0}, 1.0f/60, Trace, &wall);
    check(std::fabs(offset.x - 15) < 0.001f, "normal approach leaves ten world units before the wall");
    wall.distance = 10;
    offset = clamp.Apply({}, {30,0,0}, 1.0f/60, Trace, &wall);
    check(offset.x == 0, "tightening reaches zero immediately");
    wall.distance = 1000;
    offset = clamp.Apply({}, {30,0,0}, 1.0f/60, Trace, &wall);
    check(offset.x > 0 && offset.x < 30, "release uses core smoothing");
    const float released = offset.x;
    offset = clamp.Apply({}, {30,0,0}, 0, Trace, &wall);
    check(offset.x == released, "another render pass does not advance release smoothing");
    clamp.Reset();
    wall = {45, -0.5f};
    offset = clamp.Apply({}, {30,0,0}, 1.0f/60, Trace, &wall);
    check(std::fabs(offset.x - 25) < 0.001f, "oblique wall beyond lean plus margin is found and holds normal clearance");
    check(wall.traced >= 45, "trace reaches beyond the requested lean");
    clamp.Reset();
    wall = {110, 0.01f};
    offset = clamp.Apply({}, {30,0,0}, 1.0f/60, Trace, &wall);
    check(std::fabs(offset.x - 10) < 0.001f, "grazing approach uses bounded overreach and standoff");
    wall.available = false;
    offset = clamp.Apply({}, {30,0,0}, 1.0f/60, Trace, &wall);
    check(offset.x == 30 && clamp.LastQueryFailed(), "missing physics world is a reported failure");
    clamp.Reset();
    wall = {1000, 1};
    offset = clamp.Apply({}, {30,0,0}, 1.0f/60, Trace, &wall);
    check(offset.x == 30 && !clamp.InContact(), "camera cut clears the previous allowance");
    return failed ? 1 : 0;
}
