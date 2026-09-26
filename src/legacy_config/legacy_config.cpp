// SPDX-License-Identifier: MIT

#include "pch.h"
#include "legacy_config.h"
#include "core/logging.h"

#include <cameraunlock/config/ini_reader.h>

#include <algorithm>
#include <cmath>

namespace Fallout4HT::legacy {

namespace {

inline float SanitizeFinite(float value, float fallback, float lo, float hi) {
    return std::clamp(std::isfinite(value) ? value : fallback, lo, hi);
}

// std::clamp does not sanitize NaN - clamp(NaN, lo, hi) returns NaN, because
// both comparisons it makes are false. The INI values arrive through strtod,
// which accepts "nan"/"inf" and overflows 1e400 to +inf, and every one of them
// feeds the rotation matrices this mod writes straight into the engine's scene
// graph. SanitizeFinite substitutes the default before clamping.
void Validate(Config& c) {
    const Config defaults{};

    c.yawMultiplier = SanitizeFinite(c.yawMultiplier, defaults.yawMultiplier, 0.1f, 5.0f);
    c.pitchMultiplier = SanitizeFinite(c.pitchMultiplier, defaults.pitchMultiplier, 0.1f, 5.0f);
    c.rollMultiplier = SanitizeFinite(c.rollMultiplier, defaults.rollMultiplier, 0.0f, 2.0f);

    c.localSmoothing = SanitizeFinite(c.localSmoothing, defaults.localSmoothing, 0.0f, 1.0f);
    c.remoteSmoothing = SanitizeFinite(c.remoteSmoothing, defaults.remoteSmoothing, 0.0f, 1.0f);

    c.positionSensitivityX = SanitizeFinite(c.positionSensitivityX, defaults.positionSensitivityX, 0.1f, 10.0f);
    c.positionSensitivityY = SanitizeFinite(c.positionSensitivityY, defaults.positionSensitivityY, 0.1f, 10.0f);
    c.positionSensitivityZ = SanitizeFinite(c.positionSensitivityZ, defaults.positionSensitivityZ, 0.1f, 10.0f);

    c.positionLimitX = SanitizeFinite(c.positionLimitX, defaults.positionLimitX, 0.01f, 2.0f);
    c.positionLimitY = SanitizeFinite(c.positionLimitY, defaults.positionLimitY, 0.01f, 2.0f);
    c.positionLimitZ = SanitizeFinite(c.positionLimitZ, defaults.positionLimitZ, 0.01f, 2.0f);
    c.positionLimitZBack = SanitizeFinite(c.positionLimitZBack, defaults.positionLimitZBack, 0.01f, 2.0f);
}

// Warned once per process rather than once per load: config is reloadable, and
// repeating this on every reload buries it.
//
// The old value is deliberately NOT migrated into the new keys. The single
// smoothing value carried a hidden 0.15 floor, so the number in an existing
// config does not mean what it used to: copying it across would hand a local
// user smoothing they never chose under the new semantics, and copying it into
// only one of the two keys would be a guess about which connection they were on.
void WarnRetiredSmoothingKey(const cameraunlock::IniReader& reader,
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

}  // namespace

ReadStatus Read(const char* path, Config& cfg) {
    cfg = Config{};

    cameraunlock::IniReader ini;
    if (!ini.Open(path)) {
        Log::Line("WARN: Could not load config from %s, using defaults", path);
        return ReadStatus::Absent;
    }

    // Struct member defaults double as the read defaults, so missing keys
    // keep their documented values.
    // The port is read wide and range-checked so an out-of-range value can't
    // silently truncate through the uint16_t cast and bind a different port
    // than the user asked for.
    int port = ini.ReadInt("Network", "UDPPort", cfg.udpPort);
    if (port >= 1024 && port <= 65535) {
        cfg.udpPort = static_cast<uint16_t>(port);
    } else {
        Log::Line("WARN: UDPPort %d out of range [1024-65535], keeping %d", port, cfg.udpPort);
    }

    cfg.yawMultiplier = ini.ReadFloat("Sensitivity", "YawMultiplier", cfg.yawMultiplier);
    cfg.pitchMultiplier = ini.ReadFloat("Sensitivity", "PitchMultiplier", cfg.pitchMultiplier);
    cfg.rollMultiplier = ini.ReadFloat("Sensitivity", "RollMultiplier", cfg.rollMultiplier);
    cfg.localSmoothing = ini.ReadFloat("Sensitivity", "LocalSmoothing", cfg.localSmoothing);
    cfg.remoteSmoothing = ini.ReadFloat("Sensitivity", "RemoteSmoothing", cfg.remoteSmoothing);
    WarnRetiredSmoothingKey(ini, "Sensitivity", "RotationSmoothing");
    WarnRetiredSmoothingKey(ini, "Position", "Smoothing");

    cfg.toggleKey = ini.ReadHex("Hotkeys", "ToggleKey", cfg.toggleKey);
    cfg.positionToggleKey = ini.ReadHex("Hotkeys", "PositionToggleKey", cfg.positionToggleKey);
    cfg.yawModeKey = ini.ReadHex("Hotkeys", "YawModeKey", cfg.yawModeKey);

    cfg.positionSensitivityX = ini.ReadFloat("Position", "SensitivityX", cfg.positionSensitivityX);
    cfg.positionSensitivityY = ini.ReadFloat("Position", "SensitivityY", cfg.positionSensitivityY);
    cfg.positionSensitivityZ = ini.ReadFloat("Position", "SensitivityZ", cfg.positionSensitivityZ);
    cfg.positionLimitX = ini.ReadFloat("Position", "LimitX", cfg.positionLimitX);
    cfg.positionLimitY = ini.ReadFloat("Position", "LimitY", cfg.positionLimitY);
    cfg.positionLimitZ = ini.ReadFloat("Position", "LimitZ", cfg.positionLimitZ);
    cfg.positionLimitZBack = ini.ReadFloat("Position", "LimitZBack", cfg.positionLimitZBack);
    cfg.positionInvertX = ini.ReadBool("Position", "InvertX", cfg.positionInvertX);
    cfg.positionInvertY = ini.ReadBool("Position", "InvertY", cfg.positionInvertY);
    cfg.positionInvertZ = ini.ReadBool("Position", "InvertZ", cfg.positionInvertZ);
    cfg.positionEnabled = ini.ReadBool("Position", "Enabled", cfg.positionEnabled);

    cfg.autoEnable = ini.ReadBool("General", "AutoEnable", cfg.autoEnable);
    cfg.showNotifications = ini.ReadBool("General", "ShowNotifications", cfg.showNotifications);
    cfg.worldSpaceYaw = ini.ReadBool("General", "WorldSpaceYaw", cfg.worldSpaceYaw);

    Validate(cfg);
    Log::Line("Config loaded from %s", path);
    return ReadStatus::Read;
}

std::vector<cameraunlock::config::LegacyKey> ReadKeys() {
    return {
        {"Network", "UDPPort"},
        {"Sensitivity", "YawMultiplier"},
        {"Sensitivity", "PitchMultiplier"},
        {"Sensitivity", "RollMultiplier"},
        {"Sensitivity", "LocalSmoothing"},
        {"Sensitivity", "RemoteSmoothing"},
        {"Hotkeys", "ToggleKey"},
        {"Hotkeys", "PositionToggleKey"},
        {"Hotkeys", "YawModeKey"},
        {"Position", "SensitivityX"},
        {"Position", "SensitivityY"},
        {"Position", "SensitivityZ"},
        {"Position", "LimitX"},
        {"Position", "LimitY"},
        {"Position", "LimitZ"},
        {"Position", "LimitZBack"},
        {"Position", "InvertX"},
        {"Position", "InvertY"},
        {"Position", "InvertZ"},
        {"Position", "Enabled"},
        {"General", "AutoEnable"},
        {"General", "ShowNotifications"},
        {"General", "WorldSpaceYaw"},
    };
}

}  // namespace Fallout4HT::legacy
