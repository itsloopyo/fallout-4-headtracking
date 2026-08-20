// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

// The defaults below are the constants, so the dependency is declared here
// rather than left to whatever the including translation unit happens to have
// pulled in through the precompiled header.
#include "constants.h"

namespace Fallout4HT {

struct Config {
    // Network settings
    uint16_t udpPort = DEFAULT_UDP_PORT;

    // Sensitivity multipliers
    float yawMultiplier = 1.0f;
    float pitchMultiplier = 1.0f;
    float rollMultiplier = 1.0f;

    // Smoothing is picked per connection from the packet's source address, and
    // both values cover rotation and position alike. A tracker running on this
    // machine needs none of it - localSmoothing is 0.0 and nothing floors it -
    // while a phone on WiFi jitters over the network, which is what
    // remoteSmoothing is for. Higher trades crispness for noise rejection.
    float localSmoothing = 0.0f;
    float remoteSmoothing = 0.15f;

    // Hotkeys (Virtual Key codes)
    int toggleKey = DEFAULT_TOGGLE_KEY;
    int positionToggleKey = DEFAULT_POSITION_TOGGLE_KEY;
    int yawModeKey = DEFAULT_YAW_MODE_KEY;

    // Position settings (6DOF)
    float positionSensitivityX = 1.0f;
    float positionSensitivityY = 1.0f;
    float positionSensitivityZ = 1.0f;
    float positionLimitX = 0.30f;
    float positionLimitY = 0.20f;
    float positionLimitZ = 0.40f;
    float positionLimitZBack = 0.10f;
    // X stays inverted: a phone tracker looks at the player through its front
    // camera, so its x runs the other way round, which is what this setting is
    // for. Its limit is symmetric, so inverting costs nothing.
    bool positionInvertX = true;
    bool positionInvertY = false;
    // Z must NOT be inverted here. The core-to-engine convention flip is a
    // negation at the engine boundary (TrackerLeanToWorldUnits); doing it with
    // this flag instead swaps the asymmetric forward/back limits over with it,
    // so leaning in stops at 0.10 m while leaning back gets 0.40 m.
    bool positionInvertZ = false;
    bool positionEnabled = true;

    // General settings
    bool autoEnable = true;
    bool showNotifications = true;
    bool worldSpaceYaw = true;

    // Load/Save
    bool Load(const char* path);
    bool Save(const char* path) const;
    void SetDefaults();
    void Validate();
};

} // namespace Fallout4HT
