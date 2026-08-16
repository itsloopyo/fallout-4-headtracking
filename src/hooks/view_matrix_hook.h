// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

#include "module_scan.h"

namespace Fallout4HT {

// Applies the current tracking pose only while Fallout builds the player view
// matrix. Engine and gameplay code otherwise see the body-aimed camera.
bool InstallViewMatrixHook(const TextSection& text, uintptr_t moduleBase);
void RemoveViewMatrixHook();

void ReportViewMatrixStats();

} // namespace Fallout4HT
