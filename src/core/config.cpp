// SPDX-License-Identifier: MIT

#include "pch.h"
#include "config.h"
#include "logging.h"

#include <cameraunlock/config/ini_reader.h>
#include <cameraunlock/math/finite_utils.h>

namespace Fallout4HT {

// Inline member initializers on the Config struct are the single source of truth
// for defaults. SetDefaults() resets the whole struct to its freshly-constructed state.
void Config::SetDefaults() {
    *this = Config{};
}

// std::clamp does not sanitize NaN - clamp(NaN, lo, hi) returns NaN, because
// both comparisons it makes are false. The INI values arrive through strtod,
// which accepts "nan"/"inf" and overflows 1e400 to +inf, and every one of them
// feeds the rotation matrices this mod writes straight into the engine's scene
// graph. SanitizeFinite substitutes the default before clamping.
void Config::Validate() {
    using cameraunlock::math::SanitizeFinite;
    const Config defaults{};

    yawMultiplier = SanitizeFinite(yawMultiplier, defaults.yawMultiplier, 0.1f, 5.0f);
    pitchMultiplier = SanitizeFinite(pitchMultiplier, defaults.pitchMultiplier, 0.1f, 5.0f);
    rollMultiplier = SanitizeFinite(rollMultiplier, defaults.rollMultiplier, 0.0f, 2.0f);

    localSmoothing = SanitizeFinite(localSmoothing, defaults.localSmoothing, 0.0f, 1.0f);
    remoteSmoothing = SanitizeFinite(remoteSmoothing, defaults.remoteSmoothing, 0.0f, 1.0f);

    positionSensitivityX = SanitizeFinite(positionSensitivityX, defaults.positionSensitivityX, 0.1f, 10.0f);
    positionSensitivityY = SanitizeFinite(positionSensitivityY, defaults.positionSensitivityY, 0.1f, 10.0f);
    positionSensitivityZ = SanitizeFinite(positionSensitivityZ, defaults.positionSensitivityZ, 0.1f, 10.0f);

    positionLimitX = SanitizeFinite(positionLimitX, defaults.positionLimitX, 0.01f, 2.0f);
    positionLimitY = SanitizeFinite(positionLimitY, defaults.positionLimitY, 0.01f, 2.0f);
    positionLimitZ = SanitizeFinite(positionLimitZ, defaults.positionLimitZ, 0.01f, 2.0f);
    positionLimitZBack = SanitizeFinite(positionLimitZBack, defaults.positionLimitZBack, 0.01f, 2.0f);
}

// Warned once per process rather than once per load: config is reloadable, and
// repeating this on every reload buries it.
//
// The old value is deliberately NOT migrated into the new keys. The single
// smoothing value carried a hidden 0.15 floor, so the number in an existing
// config does not mean what it used to: copying it across would hand a local
// user smoothing they never chose under the new semantics, and copying it into
// only one of the two keys would be a guess about which connection they were on.
static void WarnRetiredSmoothingKey(const cameraunlock::IniReader& reader,
                                    const char* section, const char* key) {
    static bool warned = false;
    if (warned) return;
    if (reader.ReadString(section, key, "").empty()) return;
    warned = true;
    Log::Line(
        "WARN: Config key [%s] %s has been retired and is IGNORED. Smoothing is now two "
        "keys: LocalSmoothing (default 0, applies to a tracker on this machine) and "
        "RemoteSmoothing (default 0.15, applies to a tracker on the network). The "
        "old value is not migrated because the semantics changed - it carried a "
        "hidden 0.15 floor that no longer exists. Set the two new keys.",
        section, key);
}

bool Config::Load(const char* path) {
    SetDefaults();

    cameraunlock::IniReader ini;
    if (!ini.Open(path)) {
        Log::Line("WARN: Could not load config from %s, using defaults", path);
        return false;
    }

    // Struct member defaults double as the read defaults, so missing keys
    // keep their documented values.
    // The port is read wide and range-checked so an out-of-range value can't
    // silently truncate through the uint16_t cast and bind a different port
    // than the user asked for.
    int port = ini.ReadInt("Network", "UDPPort", udpPort);
    if (port >= 1024 && port <= 65535) {
        udpPort = static_cast<uint16_t>(port);
    } else {
        Log::Line("WARN: UDPPort %d out of range [1024-65535], keeping %d", port, udpPort);
    }

    yawMultiplier = ini.ReadFloat("Sensitivity", "YawMultiplier", yawMultiplier);
    pitchMultiplier = ini.ReadFloat("Sensitivity", "PitchMultiplier", pitchMultiplier);
    rollMultiplier = ini.ReadFloat("Sensitivity", "RollMultiplier", rollMultiplier);
    localSmoothing = ini.ReadFloat("Sensitivity", "LocalSmoothing", localSmoothing);
    remoteSmoothing = ini.ReadFloat("Sensitivity", "RemoteSmoothing", remoteSmoothing);
    WarnRetiredSmoothingKey(ini, "Sensitivity", "RotationSmoothing");
    WarnRetiredSmoothingKey(ini, "Position", "Smoothing");

    toggleKey = ini.ReadHex("Hotkeys", "ToggleKey", toggleKey);
    positionToggleKey = ini.ReadHex("Hotkeys", "PositionToggleKey", positionToggleKey);
    yawModeKey = ini.ReadHex("Hotkeys", "YawModeKey", yawModeKey);

    positionSensitivityX = ini.ReadFloat("Position", "SensitivityX", positionSensitivityX);
    positionSensitivityY = ini.ReadFloat("Position", "SensitivityY", positionSensitivityY);
    positionSensitivityZ = ini.ReadFloat("Position", "SensitivityZ", positionSensitivityZ);
    positionLimitX = ini.ReadFloat("Position", "LimitX", positionLimitX);
    positionLimitY = ini.ReadFloat("Position", "LimitY", positionLimitY);
    positionLimitZ = ini.ReadFloat("Position", "LimitZ", positionLimitZ);
    positionLimitZBack = ini.ReadFloat("Position", "LimitZBack", positionLimitZBack);
    positionInvertX = ini.ReadBool("Position", "InvertX", positionInvertX);
    positionInvertY = ini.ReadBool("Position", "InvertY", positionInvertY);
    positionInvertZ = ini.ReadBool("Position", "InvertZ", positionInvertZ);
    positionEnabled = ini.ReadBool("Position", "Enabled", positionEnabled);

    autoEnable = ini.ReadBool("General", "AutoEnable", autoEnable);
    showNotifications = ini.ReadBool("General", "ShowNotifications", showNotifications);
    worldSpaceYaw = ini.ReadBool("General", "WorldSpaceYaw", worldSpaceYaw);

    Validate();
    Log::Line("Config loaded from %s", path);
    return true;
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

} // namespace Fallout4HT
