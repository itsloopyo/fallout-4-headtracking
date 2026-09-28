// SPDX-License-Identifier: MIT

#include "pch.h"
#include "hotkeys.h"
#include "mod.h"
#include "logging.h"
#include "diagnostics/ab_switches.h"
#include "diagnostics/frame_verdict.h"
#include "diagnostics/matrix_dump.h"
#include "diagnostics/pose_trace.h"
#include "diagnostics/vats_probe.h"
#include "diagnostics/write_watch.h"
#include "game/fallout4_types.h"
#include "hooks/camera_snapshot.h"
#include "diagnostics/render_audit.h"

#include <cameraunlock/input/key_binding_registration.h>
#include <cameraunlock/input/key_bindings.h>

#include <functional>
#include <stdexcept>
#include <string>

// The diagnostic chords below are reverse-engineering instruments,
// not controls a player needs. Configure with -DFALLOUT4_DEV_HOTKEYS=ON to
// re-arm them.
#ifndef FALLOUT4_DEV_HOTKEYS
#define FALLOUT4_DEV_HOTKEYS 0
#endif

#if FALLOUT4_DEV_HOTKEYS
#include <cameraunlock/input/chord_hotkeys.h>
#endif

namespace Fallout4HT {

namespace {

// A key list from CameraUnlock.ini onto the poller. The table only holds lists its
// hotkey codec read, so one that does not parse here is a bug, not a player's typo.
void Register(cameraunlock::input::HotkeyPoller& poller, const std::string& list, const char* key,
              std::function<void()> action) {
    const cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(list);
    if (!parsed.ok()) {
        throw std::logic_error(std::string("[Hotkeys] ") + key + "=" + list + " does not parse: " + parsed.error);
    }
    cameraunlock::input::RegisterKeyBindings(poller, parsed.bindings, std::move(action));
}

}  // namespace

bool Hotkeys::Start(const Config& cfg) {
    if (m_started) return true;

    // Each list holds every key that fires its action, the Ctrl+Shift chord
    // included, and a key without modifiers stays silent while Ctrl and Shift
    // are both held, so one press never fires two actions.
    Register(m_poller, cfg.toggle_key_name, "ToggleKey", [] { Mod::Instance().Toggle(); });
    Register(m_poller, cfg.cycle_tracking_mode_key_name, "CycleTrackingModeKey",
             [] { Mod::Instance().CycleDofMode(); });
    Register(m_poller, cfg.yaw_mode_key_name, "YawModeKey", [] { Mod::Instance().ToggleYawMode(); });
    Register(m_poller, cfg.true_free_look_key_name, "TrueFreeLookKey",
             [] { Mod::Instance().ToggleTrueFreeLook(); });
    // Needed because which tracker app wins the source lock is a race decided in
    // milliseconds at startup, so a player running more than one (OpenTrack plus
    // a vendor tool) can end up on the wrong one with no way to say so from
    // inside the game.
    Register(m_poller, cfg.cycle_tracker_source_key_name, "CycleTrackerSourceKey",
             [] { Mod::Instance().CycleTrackerSource(); });

#if FALLOUT4_DEV_HOTKEYS
    using cameraunlock::input::ChordGuarded;

    // Diagnostics are chords too, and for a harder reason than tidiness: F5 is
    // Fallout 4's quicksave and F9 its quickload. Diagnostics sat on both, so
    // arming an instrument saved the game and running an A/B reloaded it - which
    // silently wrecked several days of measurements before anyone noticed.
    // Ctrl+Shift+<letter> is the one modifier combination no game binds.
    m_poller.AddHotkey('D', ChordGuarded([] { DumpPoseTrace(); }));
    m_poller.AddHotkey('L', ChordGuarded([] { Mod::Instance().ToggleExtrapolation(); }));
    m_poller.AddHotkey('I', ChordGuarded([] { Mod::Instance().CycleAxisIsolation(); }));
    m_poller.AddHotkey('K', ChordGuarded([] { AbSwitches::ToggleCrosshairMove(); }));
    m_poller.AddHotkey('B', ChordGuarded([] { DumpFrameVerdictTrace(); }));
    m_poller.AddHotkey('V', ChordGuarded([] { ProbeVats(); }));
    m_poller.AddHotkey('X', ChordGuarded([] { ProbeVatsInVats(); }));
    m_poller.AddHotkey('N', ChordGuarded([] { AbSwitches::ToggleStripPoseInCleanScope(); }));
    m_poller.AddHotkey('M', ChordGuarded([] { AbSwitches::ToggleStageRuler(); }));

    // Ctrl+Shift+W: report what writes cameraRoot's world rotation. Whatever
    // rebuilds it to the body's orientation each frame is the last uncovered
    // window - head tracking is applied once a tick and that write wipes it.
    m_poller.AddHotkey('W', ChordGuarded([] {
        CameraRootSnapshots snap;
        if (!GetCameraRootSnapshots(snap) || snap.cameraRoot == 0) {
            Log::Line("write watch: no camera snapshot yet");
            return;
        }
        ArmWriteWatch(reinterpret_cast<uintptr_t>(WorldRotationOf(snap.cameraRoot)), 8);
    }));

    // Ctrl+Shift+O: the same, on the LOCAL rotation. This is the one that
    // matters: the world transform is recomputed from local by a generic
    // scene-graph pass, so a local->world copy would PRESERVE head tracking.
    // The camera coming back un-tracked every frame means something resets
    // local, and that writer is the last unhooked site.
    m_poller.AddHotkey('O', ChordGuarded([] {
        CameraRootSnapshots snap;
        if (!GetCameraRootSnapshots(snap) || snap.cameraRoot == 0) {
            Log::Line("write watch: no camera snapshot yet");
            return;
        }
        ArmWriteWatch(reinterpret_cast<uintptr_t>(LocalRotationOf(snap.cameraRoot)), 8);
    }));

    m_poller.AddHotkey('P', ChordGuarded([] { DumpCameraMatrices(); ArmRenderAudit(); }));
#endif

    if (!m_poller.Start()) {
        Log::Line("ERROR: Hotkey poller failed to start");
        return false;
    }

    Log::Line("Hotkeys ready: toggle=[%s] cycle tracking mode=[%s] yaw mode=[%s] true free look=[%s]"
              " next tracker source=[%s]",
              cfg.toggle_key_name.c_str(), cfg.cycle_tracking_mode_key_name.c_str(),
              cfg.yaw_mode_key_name.c_str(), cfg.true_free_look_key_name.c_str(),
              cfg.cycle_tracker_source_key_name.c_str());
#if FALLOUT4_DEV_HOTKEYS
    Log::Line("Diagnostics: Ctrl+Shift+D pose trace, L extrapolation, "
              "I axis isolation, K crosshair A/B, B verdict trace, V/X VATS probes, "
              "N clean-scope A/B, M stage ruler, W/O write watches, P matrix dump");
#endif

    m_started = true;
    return true;
}

void Hotkeys::Stop() {
    if (!m_started) return;
    m_poller.Stop();
    m_started = false;
}

} // namespace Fallout4HT
