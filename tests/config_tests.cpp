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
#include <limits>
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

const float kNan = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();

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

void ValidateRejectsNonFinite() {
    std::printf("Config::Validate - non-finite input\n");
    const Config defaults{};

    Config c;
    c.yawMultiplier = kNan;
    c.pitchMultiplier = kInf;
    c.rollMultiplier = -kInf;
    c.localSmoothing = kNan;
    c.remoteSmoothing = kInf;
    c.positionSensitivityX = kInf;
    c.positionSensitivityY = kNan;
    c.positionSensitivityZ = -kInf;
    c.positionLimitX = kNan;
    c.positionLimitY = kInf;
    c.positionLimitZ = kNan;
    c.positionLimitZBack = -kInf;
    c.Validate();

    Check(AllFinite(c), "every float is finite after Validate");
    Check(c.yawMultiplier == defaults.yawMultiplier, "NaN yaw -> default");
    Check(c.pitchMultiplier == defaults.pitchMultiplier, "+Inf pitch -> default");
    Check(c.rollMultiplier == defaults.rollMultiplier, "-Inf roll -> default");
    Check(c.localSmoothing == defaults.localSmoothing, "NaN local smoothing -> default");
    Check(c.remoteSmoothing == defaults.remoteSmoothing, "Inf remote smoothing -> default");
    Check(c.positionSensitivityX == defaults.positionSensitivityX, "Inf position sensitivity -> default");
    Check(c.positionLimitZ == defaults.positionLimitZ, "NaN position limit -> default");
}

void ValidateClampsFiniteOutOfRange() {
    std::printf("Config::Validate - finite out-of-range input\n");

    Config c;
    c.yawMultiplier = 99.0f;
    c.pitchMultiplier = 0.0f;
    c.rollMultiplier = -3.0f;
    c.localSmoothing = 5.0f;
    c.remoteSmoothing = -1.0f;
    c.positionLimitZ = 1.0e30f;
    c.Validate();

    Check(c.yawMultiplier == 5.0f, "yaw clamps to 5.0");
    Check(c.pitchMultiplier == 0.1f, "pitch clamps to 0.1");
    Check(c.rollMultiplier == 0.0f, "roll clamps to 0.0");
    Check(c.localSmoothing == 1.0f, "local smoothing clamps to 1.0");
    Check(c.remoteSmoothing == 0.0f, "remote smoothing clamps to 0.0");
    Check(c.positionLimitZ == 2.0f, "position limit clamps to 2.0");
}

void ValidateLeavesGoodValuesAlone() {
    std::printf("Config::Validate - in-range input\n");

    Config c;
    c.yawMultiplier = 1.5f;
    c.pitchMultiplier = 0.8f;
    c.rollMultiplier = 0.5f;
    c.localSmoothing = 0.3f;
    c.remoteSmoothing = 0.4f;
    c.positionSensitivityZ = 2.0f;
    c.positionLimitY = 0.25f;
    c.Validate();

    Check(c.yawMultiplier == 1.5f, "in-range yaw survives");
    Check(c.pitchMultiplier == 0.8f, "in-range pitch survives");
    Check(c.rollMultiplier == 0.5f, "in-range roll survives");
    Check(c.localSmoothing == 0.3f, "in-range local smoothing survives");
    Check(c.remoteSmoothing == 0.4f, "in-range remote smoothing survives");
    Check(c.positionSensitivityZ == 2.0f, "in-range position sensitivity survives");
    Check(c.positionLimitY == 0.25f, "in-range position limit survives");
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

}  // namespace

int main() {
    std::printf("Fallout4HeadTracking config tests\n=================================\n");
    ValidateRejectsNonFinite();
    ValidateClampsFiniteOutOfRange();
    ValidateLeavesGoodValuesAlone();
    LoadSanitizesHostileIni();
    LoadMissingFileKeepsDefaults();
    SaveLoadRoundTrip();

    if (g_failures == 0) {
        std::printf("All tests passed!\n");
        return 0;
    }
    std::printf("%d test(s) FAILED\n", g_failures);
    return 1;
}
