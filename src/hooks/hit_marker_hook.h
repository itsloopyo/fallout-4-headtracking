// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

namespace Fallout4HT {

void InstallHitMarkerHook(uintptr_t moduleBase, uintptr_t scopeInit,
                          uintptr_t scopeApply, bool horizontalScaleCapped);
void RemoveHitMarkerHook();
void PublishHitMarkerView(uintptr_t niCamera);

}
