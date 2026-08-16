// SPDX-License-Identifier: MIT

#include "pch.h"
#include "ab_switches.h"
#include "core/logging.h"
#include "ui/notification.h"

namespace Fallout4HT {
namespace AbSwitches {
namespace {

// Read on the camera thread, written from the hotkey thread.
std::atomic<bool> g_crosshairMove{true};
std::atomic<bool> g_stripPoseInCleanScope{false};

void Announce(const char* name, bool on) {
    Log::Line("A/B: %s %s", name, on ? "ON" : "OFF");
    std::string msg = name;
    msg += on ? ": ON" : ": OFF";
    ShowNotification(msg.c_str());
}

} // namespace


bool CrosshairMoveEnabled() { return g_crosshairMove.load(std::memory_order_relaxed); }

bool StripPoseInCleanScope() {
    return g_stripPoseInCleanScope.load(std::memory_order_relaxed);
}

void ToggleStripPoseInCleanScope() {
    const bool on = !g_stripPoseInCleanScope.load();
    g_stripPoseInCleanScope.store(on);
    Announce("Strip head pose during player update (the old flicker)", on);
}

void ToggleCrosshairMove() {
    const bool on = !g_crosshairMove.load();
    g_crosshairMove.store(on);
    Announce("Reticle follows aim", on);
}

} // namespace AbSwitches
} // namespace Fallout4HT
