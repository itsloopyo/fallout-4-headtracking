// SPDX-License-Identifier: MIT
//
// Boundary tests for the HeadTracking.ini -> Config path. Every float here ends
// up in a rotation matrix that the camera hook writes straight into the engine's
// scene graph, so a non-finite value that survives Load() is not a cosmetic
// problem: it propagates through cameraRoot into worldToCam and the rendered
// view never recovers.
//
// constants.h before config.h: config.h takes its defaults from the constants
// and the shipped build gets them through the precompiled header.

#include "core/constants.h"
#include "core/config.h"

#include <Windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using Fallout4HT::Config;

namespace {

int g_failures = 0;

void Check(bool cond, const char* what) {
    if (!cond) {
        std::printf("  FAIL: %s\n", what);
        ++g_failures;
    }
}

// A fresh path in %TEMP% per call. Distinct paths also keep Windows' private
// profile cache out of the way of the read-back assertions.
std::string TempIniPath() {
    char dir[MAX_PATH] = {};
    GetTempPathA(sizeof(dir), dir);
    char path[MAX_PATH] = {};
    GetTempFileNameA(dir, "f4ht", 0, path);
    return std::string(path);
}

void WriteFileText(const std::string& path, const char* body) {
    FILE* f = nullptr;
    fopen_s(&f, path.c_str(), "wb");
    if (!f) {
        std::printf("  FAIL: could not write %s\n", path.c_str());
        ++g_failures;
        return;
    }
    std::fwrite(body, 1, std::strlen(body), f);
    std::fclose(f);
}

bool AllFinite(const Config& c) {
    return std::isfinite(c.yawMultiplier) && std::isfinite(c.pitchMultiplier)
        && std::isfinite(c.rollMultiplier)
        && std::isfinite(c.localSmoothing) && std::isfinite(c.remoteSmoothing)
        && std::isfinite(c.positionSensitivityX) && std::isfinite(c.positionSensitivityY)
        && std::isfinite(c.positionSensitivityZ)
        && std::isfinite(c.positionLimitX) && std::isfinite(c.positionLimitY)
        && std::isfinite(c.positionLimitZ) && std::isfinite(c.positionLimitZBack);
}

// strtod, which IniReader parses floats with, accepts "nan" and "inf" and
// overflows 1e400 to +inf. This is the end-to-end version of the checks above:
// a hand-edited or corrupted INI must not be able to put either into the config.
void LoadSanitizesHostileIni() {
    std::printf("Config::Load - hostile INI\n");
    const std::string path = TempIniPath();
    WriteFileText(path,
        "[Network]\r\n"
        "UDPPort=70000\r\n"
        "[Sensitivity]\r\n"
        "YawMultiplier=nan\r\n"
        "PitchMultiplier=inf\r\n"
        "RollMultiplier=-inf\r\n"
        "LocalSmoothing=1e400\r\n"
        "RemoteSmoothing=nan\r\n"
        "[Position]\r\n"
        "SensitivityX=nan\r\n"
        "SensitivityY=1e400\r\n"
        "SensitivityZ=-1e400\r\n"
        "LimitX=nan\r\n"
        "LimitY=inf\r\n"
        "LimitZ=1e400\r\n"
        "LimitZBack=nan\r\n");

    const Config defaults{};
    Config c;
    Check(c.Load(path.c_str()), "hostile INI still loads");
    Check(AllFinite(c), "no non-finite value survives Load");
    Check(c.udpPort == defaults.udpPort, "out-of-range UDP port keeps the default");
    Check(c.yawMultiplier >= 0.1f && c.yawMultiplier <= 5.0f, "yaw lands in range");
    Check(c.localSmoothing >= 0.0f && c.localSmoothing <= 1.0f, "local smoothing lands in range");
    Check(c.remoteSmoothing >= 0.0f && c.remoteSmoothing <= 1.0f, "remote smoothing lands in range");
    Check(c.positionLimitZ >= 0.01f && c.positionLimitZ <= 2.0f, "position limit lands in range");

    DeleteFileA(path.c_str());
}

void LoadMissingFileKeepsDefaults() {
    std::printf("Config::Load - missing file\n");
    const Config defaults{};

    Config c;
    c.yawMultiplier = 4.0f;
    const bool loaded = c.Load("Z:\\fallout4-headtracking-does-not-exist\\HeadTracking.ini");

    Check(!loaded, "a missing file reports failure");
    Check(c.yawMultiplier == defaults.yawMultiplier, "state resets to defaults");
    Check(c.udpPort == defaults.udpPort, "port resets to the default");
}

// The sanitization must not cost normal round-tripping: a tuned config written
// by Save has to come back unchanged.
void SaveLoadRoundTrip() {
    std::printf("Config::Save + Config::Load round trip\n");
    const std::string path = TempIniPath();

    Config out;
    out.udpPort = 5555;
    out.yawMultiplier = 1.25f;
    out.localSmoothing = 0.5f;
    out.remoteSmoothing = 0.25f;
    out.positionLimitZ = 0.6f;
    out.positionEnabled = false;
    out.worldSpaceYaw = false;
    out.toggleKey = 0x23;
    Check(out.Save(path.c_str()), "Save writes the file");

    Config back;
    Check(back.Load(path.c_str()), "Load reads it back");
    Check(back.udpPort == 5555, "port round trips");
    Check(std::fabs(back.yawMultiplier - 1.25f) < 1e-4f, "yaw multiplier round trips");
    Check(std::fabs(back.localSmoothing - 0.5f) < 1e-4f, "local smoothing round trips");
    Check(std::fabs(back.remoteSmoothing - 0.25f) < 1e-4f, "remote smoothing round trips");
    Check(std::fabs(back.positionLimitZ - 0.6f) < 1e-4f, "position limit round trips");
    Check(back.positionEnabled == false, "position enabled round trips");
    Check(back.worldSpaceYaw == false, "yaw mode round trips");
    Check(back.toggleKey == 0x23, "hotkey round trips");

    DeleteFileA(path.c_str());
}

// There is no separate LimitYDown key in this mod's INI: the single configured
// vertical limit is meant to apply both up and down, the way
// PositionSettings::Symmetric does. Left unmirrored, limit_y_down silently
// pins at the PositionSettings struct default (0.20m) no matter what LimitY
// is set to, so raising LimitY widens upward travel only.
void BuildPositionSettingsMirrorsLimitYDown() {
    std::printf("Config::BuildPositionSettings mirrors LimitY into limit_y_down\n");

    Config c;
    c.positionLimitY = 0.55f;
    const cameraunlock::PositionSettings settings = c.BuildPositionSettings();

    Check(std::fabs(settings.limit_y - 0.55f) < 1e-6f, "limit_y takes the configured value");
    Check(std::fabs(settings.limit_y_down - 0.55f) < 1e-6f,
          "limit_y_down mirrors the configured LimitY rather than staying at the struct default");
}

}  // namespace

int main() {
    std::printf("Fallout4HeadTracking config tests\n=================================\n");
    LoadSanitizesHostileIni();
    LoadMissingFileKeepsDefaults();
    SaveLoadRoundTrip();
    BuildPositionSettingsMirrorsLimitYDown();

    if (g_failures == 0) {
        std::printf("All tests passed!\n");
        return 0;
    }
    std::printf("%d test(s) FAILED\n", g_failures);
    return 1;
}
