// SPDX-License-Identifier: MIT

#pragma once

namespace Fallout4HT {

// The aim marker of free look with a marker: a small cross over the frame at
// `ndc`, where the round will land in the head-tracked view (x right, y up,
// -1..1). The HUD has no crosshair to move while the sights are up, and none at
// all behind a scope that fills the screen, so the mark is drawn over the
// finished frame. Nothing is hooked until the first call with an opacity above
// 0. Camera thread, once per camera tick; an opacity of 0 draws nothing.
void ShowAimMarker(float ndcX, float ndcY, float opacity);

}  // namespace Fallout4HT
