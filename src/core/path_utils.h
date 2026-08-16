// SPDX-License-Identifier: MIT

#pragma once

#include <string>

namespace Fallout4HT {

// Full path to a file in the same directory as our DLL. Empty on failure, so a
// caller can tell rather than silently falling back to the game's CWD.
std::string GetModulePath(const char* filename);

// Wide path for APIs that take wide strings (core logging::Open). Converts
// via CP_ACP so non-ASCII install paths stay intact.
std::wstring GetModulePathW(const char* filename);

} // namespace Fallout4HT
