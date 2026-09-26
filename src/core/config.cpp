// SPDX-License-Identifier: MIT

#include "pch.h"
#include "config.h"
#include "logging.h"

#include "legacy_config/legacy_config.h"

#include <cameraunlock/config/ini_reader.h>

namespace Fallout4HT {

// Inline member initializers on the Config struct are the single source of truth
// for defaults. SetDefaults() resets the whole struct to its freshly-constructed state.
void Config::SetDefaults() {
    *this = Config{};
}

// The reading half lives in legacy_config/, frozen, so the conversion to the canonical
// config format imports an old file exactly as this build reads it.
bool Config::Load(const char* path) {
    legacy::Config c;
    const legacy::ReadStatus status = legacy::Read(path, c);

    udpPort = c.udpPort;
    yawMultiplier = c.yawMultiplier;
    pitchMultiplier = c.pitchMultiplier;
    rollMultiplier = c.rollMultiplier;
    localSmoothing = c.localSmoothing;
    remoteSmoothing = c.remoteSmoothing;
    toggleKey = c.toggleKey;
    positionToggleKey = c.positionToggleKey;
    yawModeKey = c.yawModeKey;
    positionSensitivityX = c.positionSensitivityX;
    positionSensitivityY = c.positionSensitivityY;
    positionSensitivityZ = c.positionSensitivityZ;
    positionLimitX = c.positionLimitX;
    positionLimitY = c.positionLimitY;
    positionLimitZ = c.positionLimitZ;
    positionLimitZBack = c.positionLimitZBack;
    positionInvertX = c.positionInvertX;
    positionInvertY = c.positionInvertY;
    positionInvertZ = c.positionInvertZ;
    positionEnabled = c.positionEnabled;
    autoEnable = c.autoEnable;
    showNotifications = c.showNotifications;
    worldSpaceYaw = c.worldSpaceYaw;

    return status == legacy::ReadStatus::Read;
}

bool Config::Save(const char* path) const {
    cameraunlock::IniWriter w;
    if (!w.Open(path)) {
        Log::Line("ERROR: Failed to save config to %s", path);
        return false;
    }

    w.WriteComment("Fallout 4 Head Tracking Configuration");
    w.WriteComment("Delete this file to reset to defaults");
    w.WriteBlankLine();

    w.WriteSection("Network");
    w.WriteComment("UDP port for OpenTrack data (default: 4242)");
    w.WriteInt("UDPPort", udpPort);
    w.WriteBlankLine();

    w.WriteSection("Sensitivity");
    w.WriteComment("Rotation sensitivity multipliers (1.0 = 1:1)");
    w.WriteDouble("YawMultiplier", yawMultiplier);
    w.WriteDouble("PitchMultiplier", pitchMultiplier);
    w.WriteDouble("RollMultiplier", rollMultiplier);
    w.WriteComment("Smoothing applied when the tracker runs on this machine (loopback).");
    w.WriteComment("0 = no smoothing, 1 = heavy. Covers rotation and position.");
    w.WriteDouble("LocalSmoothing", localSmoothing);
    w.WriteComment("Smoothing applied when the tracker is a remote device on the network.");
    w.WriteComment("0 = no smoothing, 1 = heavy. Covers rotation and position.");
    w.WriteDouble("RemoteSmoothing", remoteSmoothing);
    w.WriteBlankLine();

    w.WriteSection("Position");
    w.WriteComment("Position tracking sensitivity (0.1-10.0, higher = more movement)");
    w.WriteDouble("SensitivityX", positionSensitivityX);
    w.WriteDouble("SensitivityY", positionSensitivityY);
    w.WriteDouble("SensitivityZ", positionSensitivityZ);
    w.WriteComment("Position limits in meters (how far the camera can move)");
    w.WriteDouble("LimitX", positionLimitX);
    w.WriteDouble("LimitY", positionLimitY);
    w.WriteDouble("LimitZ", positionLimitZ);
    w.WriteComment("Backward lean limit (prevents camera clipping through player model)");
    w.WriteDouble("LimitZBack", positionLimitZBack);
    w.WriteComment("Invert position axes");
    w.WriteBool("InvertX", positionInvertX);
    w.WriteBool("InvertY", positionInvertY);
    w.WriteBool("InvertZ", positionInvertZ);
    w.WriteComment("Enable/disable position tracking (6DOF)");
    w.WriteBool("Enabled", positionEnabled);
    w.WriteBlankLine();

    w.WriteSection("Hotkeys");
    w.WriteComment("Virtual key codes (hex)");
    w.WriteComment("ToggleKey: End - Enable/disable tracking.");
    w.WriteComment("PositionToggleKey: Page Up - Toggle position. YawModeKey: Page Down - World/local yaw.");
    w.WriteHex("ToggleKey", toggleKey);
    w.WriteHex("PositionToggleKey", positionToggleKey);
    w.WriteHex("YawModeKey", yawModeKey);
    w.WriteBlankLine();

    w.WriteSection("General");
    w.WriteComment("Auto-enable tracking on game start");
    w.WriteBool("AutoEnable", autoEnable);
    w.WriteComment("Show on-screen notifications (logged to HeadTracking.log)");
    w.WriteBool("ShowNotifications", showNotifications);
    w.WriteComment("Yaw mode: true = horizon-locked (default), false = camera-local");
    w.WriteBool("WorldSpaceYaw", worldSpaceYaw);

    w.Close();
    Log::Line("Config saved to %s", path);
    return true;
}

cameraunlock::PositionSettings Config::BuildPositionSettings() const {
    cameraunlock::PositionSettings posSettings;
    posSettings.sensitivity_x = positionSensitivityX;
    posSettings.sensitivity_y = positionSensitivityY;
    posSettings.sensitivity_z = positionSensitivityZ;
    posSettings.limit_x = positionLimitX;
    posSettings.limit_y = positionLimitY;
    posSettings.limit_y_down = positionLimitY;
    posSettings.limit_z = positionLimitZ;
    posSettings.limit_z_back = positionLimitZBack;
    posSettings.invert_x = positionInvertX;
    posSettings.invert_y = positionInvertY;
    posSettings.invert_z = positionInvertZ;
    return posSettings;
}

} // namespace Fallout4HT
