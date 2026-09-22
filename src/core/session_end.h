// SPDX-License-Identifier: MIT

#pragma once

namespace Fallout4HT {

// Writes one line to the log when the game shuts itself down, so that a log
// which simply stops can be read as a crash. Safe to call once, after MinHook
// is initialised. See session_end.cpp for why this is a hook and not
// DLL_PROCESS_DETACH.
void InstallSessionEndMarker();

} // namespace Fallout4HT
