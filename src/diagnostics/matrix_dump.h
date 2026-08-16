// SPDX-License-Identifier: MIT

#pragma once

namespace Fallout4HT {

// Dump the last camera tick's clean/tracked matrices, the live node state, and
// the current tracker pose to the log. Bound to Insert; runs on the hotkey
// thread, so it only reads - it never advances the tracking pipeline.
void DumpCameraMatrices();

} // namespace Fallout4HT
