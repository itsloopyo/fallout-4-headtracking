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
// viewportAspect is the game client area's width over its height. It used to be
// derived from the camera frustum, which tracks the client aspect exactly on a
// build that supports the display's shape (measured 1.6441/0.4550 against a
// 5120x1417 client) and does NOT on one that predates 21:9/32:9 support, where
// a narrower frustum is stretched over the full width. The frustum still sets
// aimNdcX/aimNdcY, so field of view is accounted for there; only the stage's
// own width needs the window.
CrosshairStageOffset ComputeCrosshairStageOffset(bool haveAim, bool aimValid,
                                                 float aimNdcX, float aimNdcY,
                                                 double viewportAspect);

} // namespace Fallout4HT
