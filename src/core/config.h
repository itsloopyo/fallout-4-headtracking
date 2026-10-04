// SPDX-License-Identifier: MIT

#pragma once

#include <cameraunlock/config/config_owner.h>
#include <cameraunlock/config/config_table.h>
#include <cameraunlock/config/defaults_file.h>
#include <cameraunlock/config/head_tracking_config.h>
#include <cameraunlock/config/legacy_import.h>

#include <string>

namespace Fallout4HT {

// Beside the .asi, which is beside Fallout4.exe.
constexpr const wchar_t* kConfigFileName = L"CameraUnlock.ini";
// The file every build before the canonical config format read, imported once while
// CameraUnlock.ini is absent and never written.
constexpr const wchar_t* kLegacyConfigFileName = L"HeadTracking.ini";
// The game's name as cameraunlock-core's data/games.json spells it.
constexpr const char* kConfigDisplayName = "Fallout 4";

struct Config : cameraunlock::HeadTrackingConfig {
    Config() {
        collision_enabled = true;
        collision_channel = 39;
        lean_clamp.skin = 10.0f;
    }
    bool show_notifications = true;
    // false: through a scope's overlay the aim mode is stock sights, whichever mode is picked.
    bool track_through_scopes = true;
    // Ctrl+Shift+U, the chord earlier builds used, is the aim mode cycle's in every shooter.
    std::string cycle_tracker_source_key_name = "Ctrl+Shift+J";
};

// The rows of CameraUnlock.ini. Only the tracking mode pair, WorldSpaceYaw and the aim mode's three
// (TrueFreeLook, FreeLookMarker, StockSights) are Writable: their hotkeys save the player's choice, and End changes the session only.
cameraunlock::config::ConfigTable<Config> MakeConfigTable();

// HeadTracking.ini as the builds before the canonical format read it (legacy_config/), mapped
// into Config.
cameraunlock::config::LegacyImport<Config> MakeLegacyImport();

// The owner's options for CameraUnlock.ini in `folder`, with HeadTracking.ini beside it as the
// legacy file. `folder` ends in a separator.
cameraunlock::config::ConfigOwnerOptions<Config> MakeConfigOwnerOptions(
    const std::wstring& folder, cameraunlock::config::DefaultsFile defaults);

}  // namespace Fallout4HT
