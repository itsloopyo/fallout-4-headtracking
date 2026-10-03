// SPDX-License-Identifier: MIT

#pragma once

namespace Fallout4HT {

// Dev builds only. Switched on by CameraUnlockInput.txt being beside the mod when
// the game starts. Without it a dev build answers to the real keyboard like any
// other.
void StartIsolatedInputIfAsked();

}  // namespace Fallout4HT
