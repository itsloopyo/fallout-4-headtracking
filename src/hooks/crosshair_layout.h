// SPDX-License-Identifier: MIT

#pragma once

namespace Fallout4HT {

// Offset from the crosshair clip's authored position, in Scaleform stage units.
struct CrosshairStageOffset {
    double dx;
    double dy;
};

// Map the body-aim NDC position onto the HUD stage.
//
// haveAim is false when there is nothing to follow (tracking off, menus,
// loading) and the crosshair belongs back where the game authored it. aimValid
// is false when the head has turned so far the body aim is behind the view, or
// the frustum could not be read; centring would claim shots go to the middle of
// the screen, which is exactly wrong, so the crosshair is parked off-screen -
// the same thing that happens naturally once the aim crosses the edge of the
// frustum.
//
// frustumRight/frustumTop is the render aspect, which tracks the client aspect
// exactly (measured 1.6441/0.4550 against a 5120x1417 client).
CrosshairStageOffset ComputeCrosshairStageOffset(bool haveAim, bool aimValid,
                                                 float aimNdcX, float aimNdcY,
                                                 float frustumRight, float frustumTop);

} // namespace Fallout4HT
