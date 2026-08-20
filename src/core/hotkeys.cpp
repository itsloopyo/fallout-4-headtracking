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

#include <cameraunlock/input/chord_hotkeys.h>

namespace Fallout4HT {

bool Hotkeys::Start(const Config& cfg) {
    if (m_started) return true;

    using cameraunlock::input::ChordGuarded;
    using cameraunlock::input::NavGuarded;

    // Primary nav-cluster bindings. NavGuarded so they stay silent while the
    // Ctrl+Shift chord is held - the chord path is then the sole trigger and a
    // single keypress can't fire two actions.
    m_poller.AddHotkey(cfg.toggleKey, NavGuarded([] { Mod::Instance().Toggle(); }));
    m_poller.AddHotkey(cfg.positionToggleKey, NavGuarded([] { Mod::Instance().CycleDofMode(); }));
    m_poller.AddHotkey(cfg.yawModeKey, NavGuarded([] { Mod::Instance().ToggleYawMode(); }));

    // Chord aliases: Ctrl+Shift+Y/G/H
    m_poller.AddHotkey('Y', ChordGuarded([] { Mod::Instance().Toggle(); }));
    m_poller.AddHotkey('G', ChordGuarded([] { Mod::Instance().CycleDofMode(); }));
    m_poller.AddHotkey('H', ChordGuarded([] { Mod::Instance().ToggleYawMode(); }));
    // 5th user-facing slot in the cluster. Needed because which tracker app wins
    // the source lock is a race decided in milliseconds at startup, so a player
    // running more than one (OpenTrack plus a vendor tool) can end up on the
    // wrong one with no way to say so from inside the game.
    m_poller.AddHotkey('U', ChordGuarded([] { Mod::Instance().CycleTrackerSource(); }));

    // Diagnostics are chords too, and for a harder reason than tidiness: F5 is
    // Fallout 4's quicksave and F9 its quickload. Diagnostics sat on both, so
    // arming an instrument saved the game and running an A/B reloaded it - which
    // silently wrecked several days of measurements before anyone noticed.
    // Ctrl+Shift+<letter> is the one modifier combination no game binds.
    m_poller.AddHotkey('D', ChordGuarded([] { DumpPoseTrace(); }));
    m_poller.AddHotkey('J', ChordGuarded([] { Mod::Instance().ToggleExtrapolation(); }));
    m_poller.AddHotkey('I', ChordGuarded([] { Mod::Instance().CycleAxisIsolation(); }));
    m_poller.AddHotkey('K', ChordGuarded([] { AbSwitches::ToggleCrosshairMove(); }));
    m_poller.AddHotkey('B', ChordGuarded([] { DumpFrameVerdictTrace(); }));
    m_poller.AddHotkey('V', ChordGuarded([] { ProbeVats(); }));
    m_poller.AddHotkey('X', ChordGuarded([] { ProbeVatsInVats(); }));
    m_poller.AddHotkey('N', ChordGuarded([] { AbSwitches::ToggleStripPoseInCleanScope(); }));

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

    // Insert is unbound in Fallout 4, so it stays as-is.
    m_poller.AddHotkey(VK_INSERT, [] { DumpCameraMatrices(); ArmRenderAudit(); });

    if (!m_poller.Start()) {
        Log::Line("ERROR: Hotkey poller failed to start");
        return false;
    }

    Log::Line("Hotkeys ready: toggle=0x%02X position=0x%02X yawmode=0x%02X "
              "+ Ctrl+Shift+Y/G/H/U chords | diagnostics: Ctrl+Shift+D pose trace,"
              " J extrapolation, I axis isolation, B verdict trace, Insert matrix dump",
              cfg.toggleKey, cfg.positionToggleKey, cfg.yawModeKey);

    m_started = true;
    return true;
}

void Hotkeys::Stop() {
    if (!m_started) return;
    m_poller.Stop();
    m_started = false;
}

} // namespace Fallout4HT
