// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

namespace Fallout4HT {

// Bethesda's Setting is { vtable, value union, const char* name }, so a .data
// slot holding a pointer to the setting's name string IS Setting::name, the
// object starts 0x10 before it and the value sits one qword before it. Keying on
// the name rather than an address is what keeps a setting off the per-build
// offset treadmill: a patch relinks the object somewhere else and the scan
// follows it.
constexpr uintptr_t kSettingNameOffset = 0x10;
constexpr uintptr_t kSettingValueOffset = 0x08;

// The name literal, wherever the linker put it: .rdata first, because that is
// where a string constant belongs, then .data for builds that copy it. Null if
// this build has no such setting. Scans whole sections, so init thread only.
const char* FindSettingName(uintptr_t moduleBase, const char* name);

} // namespace Fallout4HT
