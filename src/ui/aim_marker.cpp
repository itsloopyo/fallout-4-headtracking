// SPDX-License-Identifier: MIT

#include "pch.h"
#include "aim_marker.h"
#include "core/logging.h"

#define CAMERAUNLOCK_DX11_OVERLAY_IMPLEMENTATION
#define CAMERAUNLOCK_AIM_MARKER_DX11_IMPLEMENTATION
#include <cameraunlock/rendering/aim_marker_dx11.h>

namespace Fallout4HT {
namespace {

cameraunlock::rendering::AimMarkerDX11 g_marker;
bool g_asked = false;

void LogOverlay(const char* message) { Log::Line("aim marker: %s", message); }

}  // namespace

void ShowAimMarker(float ndcX, float ndcY, float opacity) {
    if (!(opacity > 0.0f)) {
        if (g_asked && g_marker.Ready()) g_marker.Publish(false, 0.0f, 0.0f, 0.0f);
        return;
    }
    if (!g_asked) {
        g_asked = true;
        g_marker.SetLogger(&LogOverlay);
        Log::Line("aim marker: first asked for, starting the overlay");
    }
    if (g_marker.Ensure()) g_marker.Publish(true, ndcX, ndcY, opacity);
}

}  // namespace Fallout4HT
