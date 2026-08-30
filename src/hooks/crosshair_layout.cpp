// SPDX-License-Identifier: MIT

#include "crosshair_layout.h"

namespace Fallout4HT {
namespace {

// The HUD movie is authored 1280x720, and its two axes do not scale together.
// Measured against the crosshair art, which is fixed-size and so serves as a
// ruler: the gap between opposing arms tracks H (74px at 1080 tall, 70px at
// 1024) while the arm length tracks W (27px at 1920 wide, 18px at 1280). The
// vertical scale is H/720 on every build and at every aspect, so half the
// viewport height is always 360 stage units.
//
// The HORIZONTAL scale is where the builds part company, and the next-gen
// update is what changed it. Measured with the Ctrl+Shift+M stage ruler, which
// sweeps the reticle between -640 and +640 stage units:
//
//   1.10.163 (what GOG sells)  scaleX = W/1280      - the movie is stretched,
//                                                     the stage stays 1280 wide
//                                                     and the sweep reaches both
//                                                     screen edges at 5120x1422.
//   1.11.x (next-gen)          scaleX = min(W/1280, H/720)
//                                                   - uniform above 16:9, so the
//                                                     stage widens past 1280 and
//                                                     the same sweep stops short.
//
// Half the viewport width is therefore a flat 640 on the stretched build, and
// max(640, 360*aspect) on the capped one. The two agree at and below 16:9 and
// differ by 2.03x at 32:9, which is why a single model shipped correct on one
// build and at double the X offset on the other.
constexpr double kStageHalfHeight = 360.0;
constexpr double kStageHalfWidth = 640.0;

// Far outside any plausible stage, used to hide the reticle.
constexpr double kOffScreen = 10000.0;

} // namespace

CrosshairStageOffset ComputeCrosshairStageOffset(bool haveAim, bool aimValid,
                                                 float aimNdcX, float aimNdcY,
                                                 double viewportAspect,
                                                 bool horizontalScaleCapped) {
    if (!haveAim) return {0.0, 0.0};
    if (!aimValid) return {kOffScreen, 0.0};
    if (!(viewportAspect > 0.0)) return {0.0, 0.0};

    const double widened = kStageHalfHeight * viewportAspect;
    const double halfWidth =
        (horizontalScaleCapped && widened > kStageHalfWidth) ? widened : kStageHalfWidth;

    CrosshairStageOffset out;
    out.dx = static_cast<double>(aimNdcX) * halfWidth;
    // Scaleform's Y axis points down, so an aim above centre needs -Y.
    out.dy = -static_cast<double>(aimNdcY) * kStageHalfHeight;
    return out;
}

} // namespace Fallout4HT
