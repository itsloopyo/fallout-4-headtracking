// SPDX-License-Identifier: MIT

// Dev builds only: the game takes its keyboard and mouse from a command file
// instead of the real devices, so a test session can run in the background.
#include "pch.h"

#include "isolated_input.h"
#include "core/logging.h"
#include "core/path_utils.h"

#define CAMERAUNLOCK_ISOLATED_INPUT_IMPLEMENTATION
#include <cameraunlock/dev/isolated_input.h>

namespace Fallout4HT {

namespace {

void InputLog(const char* text) {
    Log::Line("%s", text);
}

}  // namespace

void StartIsolatedInputIfAsked() {
    const std::wstring commandFile = GetModuleDirectoryW() + L"CameraUnlockInput.txt";
    if (GetFileAttributesW(commandFile.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    if (!cameraunlock::dev::StartIsolatedInput(commandFile, &InputLog)) {
        Log::Line("ERROR: isolated input was asked for and could not be installed");
    }
}

}  // namespace Fallout4HT
