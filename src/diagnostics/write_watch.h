// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

namespace Fallout4HT {

// Reports every instruction in the game that WRITES a given address, using the
// CPU's own debug registers rather than guesswork.
//
// This is how the engine's view-matrix build was found (RVA 0x16D21D0), and it is
// the way to find the other site that matters: whatever rebuilds cameraRoot's
// rotation back to the body's orientation every frame. Head tracking is applied
// once per camera tick and that rebuild wipes it, so between the two the camera
// renders un-tracked - which no amount of reasoning about the mod's own code will
// locate, because the writer is the game's.
//
// A hardware watchpoint costs nothing until it fires and needs no patching, so it
// is safe to arm in a shipped build behind a chord. It is armed for a fixed
// number of distinct hits and then disarms itself.
void ArmWriteWatch(uintptr_t address, int sizeBytes);

} // namespace Fallout4HT
