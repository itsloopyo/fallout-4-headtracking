// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <cstdint>

#include <Windows.h>

namespace Fallout4HT {

// Exception filters for guarding OUR OWN reads and writes into engine memory.
// They never wrap a call into game code: an access violation while dereferencing
// a node the engine handed us is ours to absorb, but one raised inside a game
// function is the game's, and swallowing it would leave the game half-way
// through whatever it was doing, holding whatever locks it took, with the real
// fault erased from the crash dump.
//
// The counter is passed in by the call site rather than kept here so that a hook
// faulting every frame cannot push a quieter one past its next power-of-two
// report.

// Absorbs an access violation and lets every other code propagate. This is the
// filter for an __except guarding our own memory access.
LONG SehAbsorbAccessViolation(DWORD code, const char* where, std::atomic<uint64_t>& count);

// Absorbs every exception code, so it belongs only where the guarded block is
// reached during the game's own unwind (i.e. from inside a __finally). Letting a
// second exception escape there collides the unwind and kills the process
// outright rather than producing a crash dump, and nothing useful can propagate
// out anyway.
LONG SehAbsorbAnyException(const char* where, std::atomic<uint64_t>& count);

} // namespace Fallout4HT
