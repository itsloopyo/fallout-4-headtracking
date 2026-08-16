// SPDX-License-Identifier: MIT

#include "crosshair_layout.h"

namespace Fallout4HT {
namespace {

// The HUD movie is authored 1280x720, and its two axes do not scale together.
// Measured against the crosshair art, which is fixed-size and so serves as a
// ruler: the gap between opposing arms tracks H (74px at 1080 tall, 70px at
// 1024) while the arm length tracks W (27px at 1920 wide, 18px at 1280), and at
// 2560x1080 the horizontal figures stay at their 1920 values rather than
// growing. So the vertical scale is H/720 at every aspect, and the horizontal
// scale is W/1280 capped at H/720.
//
// Half the viewport height is therefore always 360 stage units, and half its
// width is 360*aspect once past 16:9 but a flat 640 below it. Note 16:9 is
// degenerate - every candidate model agrees there - so both branches were
// checked at aspects either side of it.
constexpr double kStageHalfHeight = 360.0;
constexpr double kStageHalfWidth = 640.0;

// Far outside any plausible stage, used to hide the reticle.
constexpr double kOffScreen = 10000.0;

} // namespace

CrosshairStageOffset ComputeCrosshairStageOffset(bool haveAim, bool aimValid,
                                                 float aimNdcX, float aimNdcY,
                                                 float frustumRight, float frustumTop) {
    if (!haveAim) return {0.0, 0.0};
    if (!aimValid) return {kOffScreen, 0.0};

    // aimValid already established both frustum extents are positive.
    const double aspect = static_cast<double>(frustumRight) / frustumTop;
    const double halfWidth = kStageHalfHeight * aspect;

    CrosshairStageOffset out;
    out.dx = static_cast<double>(aimNdcX)
             * (halfWidth > kStageHalfWidth ? halfWidth : kStageHalfWidth);
    // Scaleform's Y axis points down, so an aim above centre needs -Y.
    out.dy = -static_cast<double>(aimNdcY) * kStageHalfHeight;
    return out;
}

} // namespace Fallout4HT
