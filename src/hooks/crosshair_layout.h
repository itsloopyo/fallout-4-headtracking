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
// viewportAspect is the game client area's width over its height. It is taken
// from the window rather than the camera frustum: aimNdcX is already a fraction
// of the rendered width whatever the frustum was, so the window is the only
// place the screen's shape belongs.
//
// horizontalScaleCapped says which of the two HUD scaling behaviours this build
// has - see the measurements in crosshair_layout.cpp. It changes nothing at or
// below 16:9 and doubles the X offset at 32:9, so it cannot be guessed.
CrosshairStageOffset ComputeCrosshairStageOffset(bool haveAim, bool aimValid,
                                                 float aimNdcX, float aimNdcY,
                                                 double viewportAspect,
                                                 bool horizontalScaleCapped);

} // namespace Fallout4HT
