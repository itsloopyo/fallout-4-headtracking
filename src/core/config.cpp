// SPDX-License-Identifier: MIT

#include "pch.h"
#include "config.h"

#include "legacy_config/legacy_config.h"

#include <cameraunlock/config/head_tracking_config_table.h>
#include <cameraunlock/input/key_bindings.h>
#include <cameraunlock/tracking/tracking_mode.h>

#include <utility>
#include <vector>

namespace Fallout4HT {

namespace {

namespace cfg = cameraunlock::config;
using cameraunlock::input::KeyBinding;
using cameraunlock::input::KeyModifiers;

// A legacy hotkey code and the Ctrl+Shift chord every earlier build registered beside it, as
// one key list.
std::string KeyList(int vk, char letter, const char* key, std::vector<cfg::DroppedValue>& dropped) {
    const std::string code = cfg::LegacyVirtualKeyToBindings(vk, "Hotkeys", key, dropped);
    const std::string chord =
        cameraunlock::input::FormatKeyBindings({KeyBinding{KeyModifiers::kCtrl | KeyModifiers::kShift, letter}});
    return code.empty() ? chord : code + ", " + chord;
}

cfg::ImportResult Import(const cfg::LegacyInput& input, Config& out) {
    legacy::Config c;
    const legacy::ReadStatus status = legacy::Read(input.ansi_path.c_str(), c);

    std::vector<cfg::DroppedValue> dropped;
    std::vector<cfg::PoseShapingValue> shaping;

    out.udp_port = c.udpPort;
    out.enable_on_startup = c.autoEnable;
    out.world_space_yaw = c.worldSpaceYaw;
    out.show_notifications = c.showNotifications;

    // [Position] Enabled chose only the mode the session started in: the cycle key reached
    // every mode either way.
    const cameraunlock::TrackingModeChannels mode = cameraunlock::EncodeTrackingMode(
        c.positionEnabled ? cameraunlock::TrackingMode::RotationAndPosition
                          : cameraunlock::TrackingMode::RotationOnly);
    out.rotation_enabled = mode.rotation_enabled;
    out.position_enabled = mode.position_enabled;

    // The frozen reader holds both to [0, 1] and every limit to [0.01, 2], and replaces a value
    // that is not finite with the default, so nothing here is out of a canonical range.
    out.local_smoothing = c.localSmoothing;
    out.position.local_smoothing = c.localSmoothing;
    out.remote_smoothing = c.remoteSmoothing;
    out.position.remote_smoothing = c.remoteSmoothing;

    // LimitY bounded both directions, so it becomes both explicit values.
    out.position.limit_x = c.positionLimitX;
    out.position.limit_y = c.positionLimitY;
    out.position.limit_y_down = c.positionLimitY;
    out.position.limit_z = c.positionLimitZ;
    out.position.limit_z_back = c.positionLimitZBack;

    // Every sensitivity shipped at 1.0, identity. InvertX shipped true, and that inversion is
    // now the x negation in TrackerAxesToCameraFrame; InvertY and InvertZ shipped false. A value
    // the player changed is dropped.
    const auto shape = [&](auto value, auto shipped, const char* section, const char* key) {
        cfg::LegacyPoseShaping(value, shipped, section, key, shaping, dropped);
    };
    shape(c.yawMultiplier, legacy::kDefaultMultiplier, "Sensitivity", "YawMultiplier");
    shape(c.pitchMultiplier, legacy::kDefaultMultiplier, "Sensitivity", "PitchMultiplier");
    shape(c.rollMultiplier, legacy::kDefaultMultiplier, "Sensitivity", "RollMultiplier");
    shape(c.positionSensitivityX, legacy::kDefaultPositionSensitivity, "Position", "SensitivityX");
    shape(c.positionSensitivityY, legacy::kDefaultPositionSensitivity, "Position", "SensitivityY");
    shape(c.positionSensitivityZ, legacy::kDefaultPositionSensitivity, "Position", "SensitivityZ");
    shape(c.positionInvertX, legacy::kDefaultPositionInvertX, "Position", "InvertX");
    shape(c.positionInvertY, legacy::kDefaultPositionInvertY, "Position", "InvertY");
    shape(c.positionInvertZ, legacy::kDefaultPositionInvertZ, "Position", "InvertZ");

    out.toggle_key_name = KeyList(c.toggleKey, 'Y', "ToggleKey", dropped);
    out.cycle_tracking_mode_key_name = KeyList(c.positionToggleKey, 'G', "PositionToggleKey", dropped);
    out.yaw_mode_key_name = KeyList(c.yawModeKey, 'H', "YawModeKey", dropped);

    return status == legacy::ReadStatus::Absent ? cfg::ImportResult::Absent(std::move(dropped), std::move(shaping))
                                                : cfg::ImportResult::Imported(std::move(dropped), std::move(shaping));
}

}  // namespace

cfg::ConfigTable<Config> MakeConfigTable() {
    using C = cfg::schema::Concept;
    cfg::ConfigTable<Config> table = cfg::HeadTrackingConfigTable<Config>(
        {C::UdpPort, C::EnableOnStartup, C::WorldSpaceYaw, C::RotationEnabled, C::LocalSmoothing,
         C::RemoteSmoothing, C::PositionEnabled, C::PositionLimitX, C::PositionLimitY, C::PositionLimitYDown,
         C::PositionLimitZ, C::PositionLimitZBack, C::ToggleKey, C::CycleTrackingModeKey, C::YawModeKey});
    table.Select(C::WorldSpaceYaw).Writable()
        .Select(C::RotationEnabled).Writable()
        .Select(C::PositionEnabled).Writable();
    table.Local("General", "ShowNotifications", &Config::show_notifications, cfg::BoolCodec(),
                "true: write the mod's notices (tracking on or off, a mode change) to HeadTracking.log.");
    table.Local("Hotkeys", "CycleTrackerSourceKey", &Config::cycle_tracker_source_key_name, cfg::HotkeyCodec(),
                "Switches to the next tracker app when more than one sends to the UDP port.");
    return table;
}

cfg::LegacyImport<Config> MakeLegacyImport() {
    return {&Import, legacy::ReadKeys()};
}

cfg::ConfigOwnerOptions<Config> MakeConfigOwnerOptions(const std::wstring& folder, cfg::DefaultsFile defaults) {
    cfg::ConfigOwnerOptions<Config> options;
    options.path = folder + kConfigFileName;
    options.legacy_path = folder + kLegacyConfigFileName;
    options.table = MakeConfigTable();
    options.import = MakeLegacyImport();
    options.header.display_name = kConfigDisplayName;
    options.defaults = std::move(defaults);
    return options;
}

}  // namespace Fallout4HT
