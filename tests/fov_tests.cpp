// SPDX-License-Identifier: MIT
//
// Pins the two numbers the zoom compensation rests on, both of which were
// measured against a running game rather than derived:
//
//   - the axis and reference aspect fDefault*FOV is expressed in, which decides
//     whether the whole of normal play runs at a fixed fraction of the pose
//   - that the factor is exactly 1.0 when nothing is zoomed
//
// A wrong reference aspect here is invisible in play: head tracking simply
// feels weak everywhere, which is why it is worth a test rather than a comment.

#include "game/fov_settings.h"

#include <cameraunlock/camera/zoom_compensation.h>

#include <cmath>
#include <cstdio>

using namespace Fallout4HT;

namespace {

int g_failures = 0;

void Check(bool cond, const char* what) {
    if (!cond) {
        std::printf("  FAIL: %s\n", what);
        ++g_failures;
    }
}

void CheckNear(float got, float want, float tol, const char* what) {
    if (!(std::fabs(got - want) <= tol)) {
        std::printf("  FAIL: %s (got %.5f, want %.5f)\n", what, got, want);
        ++g_failures;
    }
}

// Both measured off the live NiCamera with the setting at that value. The
// tolerance is the precision the log line reported them to.
void MeasuredSettingsMapToRenderedFrustum() {
    CheckNear(BaseTangentForFovDegrees(80.0f), 0.47199f, 0.00002f,
              "setting 80 renders tan(vfov/2) 0.47199");
    CheckNear(BaseTangentForFovDegrees(100.0f), 0.67036f, 0.00002f,
              "setting 100 renders tan(vfov/2) 0.67036");
}

// The reference is horizontal at 16:9. Reading it as a vertical angle instead
// would put every factor out by 1.7778, and nothing in play would say so.
void ReferenceIsHorizontalAtSixteenNine() {
    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
    const float asVertical = std::tan(80.0f * 0.5f * kDegToRad);
    Check(std::fabs(BaseTangentForFovDegrees(80.0f) - asVertical) > 0.3f,
          "the setting is not a vertical angle");
    CheckNear(asVertical / BaseTangentForFovDegrees(80.0f), 16.0f / 9.0f, 0.0001f,
              "the two readings differ by exactly the 16:9 reference");
}

void UnzoomedFactorIsExactlyOne() {
    const float base = BaseTangentForFovDegrees(80.0f);
    Check(cameraunlock::camera::FovZoomFactor(base, base) == 1.0f,
          "a rendered FOV equal to the base gives factor 1.0 exactly");
    Check(cameraunlock::camera::ScaleAngleForZoom(12.5f, 1.0f) > 12.49f &&
          cameraunlock::camera::ScaleAngleForZoom(12.5f, 1.0f) < 12.51f,
          "factor 1.0 leaves an angle alone");
}

// Narrowing the view must SHRINK the angle, so the picture moves by the same
// amount it would have un-zoomed. Getting this backwards doubles the problem it
// is meant to remove.
void ZoomingInShrinksTheAngle() {
    const float base = BaseTangentForFovDegrees(80.0f);
    const float scoped = BaseTangentForFovDegrees(40.0f);
    const float factor = cameraunlock::camera::FovZoomFactor(scoped, base);
    Check(factor < 1.0f, "a narrower rendered FOV gives a factor below 1");
    Check(cameraunlock::camera::ScaleAngleForZoom(10.0f, factor) < 10.0f,
          "the applied angle shrinks when zoomed in");

    // The screen displacement is what has to be preserved: tan(out)/tan(base
    // half) must equal tan(in)/tan(scoped half) once scaled.
    const float in = 10.0f;
    const float out = cameraunlock::camera::ScaleAngleForZoom(in, factor);
    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
    CheckNear(std::tan(out * kDegToRad) / scoped, std::tan(in * kDegToRad) / base, 1e-6f,
              "screen displacement is preserved across the zoom");
}

void RejectsImplausibleSettings() {
    Check(BaseTangentForFovDegrees(0.0f) == 0.0f, "zero degrees is rejected");
    Check(BaseTangentForFovDegrees(-10.0f) == 0.0f, "a negative FOV is rejected");
    Check(BaseTangentForFovDegrees(180.0f) == 0.0f, "180 degrees is rejected");
}

} // namespace

int main() {
    std::printf("fov_tests\n");
    MeasuredSettingsMapToRenderedFrustum();
    ReferenceIsHorizontalAtSixteenNine();
    UnzoomedFactorIsExactlyOne();
    ZoomingInShrinksTheAngle();
    RejectsImplausibleSettings();

    if (g_failures == 0) {
        std::printf("  all passed\n");
        return 0;
    }
    std::printf("  %d failure(s)\n", g_failures);
    return 1;
}
