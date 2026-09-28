// SPDX-License-Identifier: MIT
//
// CameraUnlock.ini against the table: the committed HeadTracking.ini is the table's fresh
// render byte for byte, the owner creates exactly those bytes, and each toggle's save changes
// the lines of its own rows and no other byte. `--render-config <path>` writes the fresh render
// to <path> instead and runs nothing else (pixi run render-config).
//
// Every owner here reads and creates a scratch Defaults.ini, never the player's own.

#include "core/config.h"

#include <cameraunlock/config/config_owner.h>
#include <cameraunlock/config/defaults_file.h>
#include <cameraunlock/input/key_bindings.h>

#include <Windows.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace cfg = cameraunlock::config;
using Fallout4HT::Config;

namespace {

int g_failures = 0;

void Check(bool cond, const char* what) {
    if (!cond) {
        std::printf("  FAIL: %s\n", what);
        ++g_failures;
    }
}

std::string ReadBytes(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read a test file");
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteBytes(const std::wstring& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write a test file");
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string FreshRender() {
    return cfg::RenderCanonicalFresh(Fallout4HT::MakeConfigTable(), cfg::RenderHeader{Fallout4HT::kConfigDisplayName});
}

// A fresh folder in %TEMP%, ending in a separator, with Defaults.ini in a folder of its own.
struct Scratch {
    std::wstring folder;
    std::wstring defaults;
};

Scratch MakeScratch(const wchar_t* name) {
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const std::wstring root = std::wstring(temp) + L"fallout4-config-tests-" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(root.c_str(), nullptr);
    const std::wstring dir = root + L"\\" + name;
    if (!CreateDirectoryW(dir.c_str(), nullptr)) throw std::runtime_error("cannot create a scratch folder");
    const std::wstring global = dir + L"\\global";
    if (!CreateDirectoryW(global.c_str(), nullptr)) throw std::runtime_error("cannot create a scratch folder");
    return {dir + L"\\", global + L"\\Defaults.ini"};
}

cfg::ConfigOwnerOptions<Config> Options(const Scratch& s) {
    return Fallout4HT::MakeConfigOwnerOptions(s.folder, cfg::DefaultsFile::At(s.defaults));
}

std::vector<std::string> Lines(const std::string& bytes) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < bytes.size()) {
        const size_t end = bytes.find("\r\n", start);
        if (end == std::string::npos) {
            lines.push_back(bytes.substr(start));
            break;
        }
        lines.push_back(bytes.substr(start, end - start));
        start = end + 2;
    }
    return lines;
}

// The lines of `after` that differ from `before`, which must have as many.
std::vector<std::string> ChangedLines(const std::string& before, const std::string& after) {
    const std::vector<std::string> a = Lines(before);
    const std::vector<std::string> b = Lines(after);
    if (a.size() != b.size()) return {"a line was added or removed"};
    std::vector<std::string> changed;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) changed.push_back(b[i]);
    }
    return changed;
}

void CommittedFileIsTheFreshRender() {
    std::printf("HeadTracking.ini, the committed file, is the table's fresh render\n");
    Check(ReadBytes(F4_COMMITTED_CONFIG) == FreshRender(), "run pixi run render-config after changing a row");
}

void EveryHotkeyDefaultParses() {
    std::printf("every hotkey list the table defaults to parses\n");
    const Config defaults = Fallout4HT::MakeConfigTable().defaults();
    for (const std::string* list : {&defaults.toggle_key_name, &defaults.cycle_tracking_mode_key_name,
                                    &defaults.yaw_mode_key_name, &defaults.true_free_look_key_name,
                                    &defaults.cycle_tracker_source_key_name}) {
        Check(cameraunlock::input::ParseKeyBindings(*list).ok(), list->c_str());
    }
    Check(defaults.toggle_key_name == "End, Ctrl+Shift+Y", "ToggleKey is the fleet's default");
    Check(defaults.cycle_tracking_mode_key_name == "PageUp, Ctrl+Shift+G", "CycleTrackingModeKey is the fleet's default");
    Check(defaults.yaw_mode_key_name == "PageDown, Ctrl+Shift+H", "YawModeKey is the fleet's default");
    Check(defaults.true_free_look_key_name == "Insert, Ctrl+Shift+U", "TrueFreeLookKey is the fleet's default");
    Check(defaults.cycle_tracker_source_key_name == "Ctrl+Shift+J",
          "CycleTrackerSourceKey moved off Ctrl+Shift+U, which is true free look's");
}

void FirstLaunchCreatesTheCommittedFile() {
    std::printf("the first launch with no file creates the committed bytes\n");
    const Scratch s = MakeScratch(L"created");
    cfg::ConfigOwner<Config> owner(Options(s));
    const cfg::ConfigLoadResult<Config> loaded = owner.Load();
    Check(loaded.status == cfg::ConfigLoadStatus::Created, "the load is Created");
    Check(ReadBytes(s.folder + Fallout4HT::kConfigFileName) == FreshRender(), "the created file is the fresh render");
    Check(GetFileAttributesW((s.folder + Fallout4HT::kLegacyConfigFileName).c_str()) == INVALID_FILE_ATTRIBUTES,
          "no HeadTracking.ini is written");
}

void CollisionSettingsFollowDefaultsAndKeepEngineUnits() {
    const Scratch s = MakeScratch(L"collision");
    WriteBytes(s.defaults, "[Position]\r\nCollisionEnabled=false\r\nCollisionReleaseSmoothing=0.4\r\n");
    cfg::ConfigOwner<Config> owner(Options(s));
    const auto loaded = owner.Load();
    Check(!loaded.config.collision_enabled, "collision switch follows Defaults.ini");
    Check(loaded.config.lean_clamp.release_smoothing == 0.4f, "collision release follows Defaults.ini");
    Check(loaded.config.lean_clamp.skin == 10.0f && loaded.config.collision_channel == 39,
          "Fallout keeps its own collision units and camera channel");
    WriteBytes(s.folder + Fallout4HT::kConfigFileName,
        "[CameraUnlock]\r\nConfigFormat=1\r\n[Position]\r\nCollisionEnabled=true\r\n"
        "CollisionMargin=20.0\r\nCollisionReleaseSmoothing=0.2\r\nCollisionChannel=36\r\n");
    cfg::ConfigOwner<Config> overrideOwner(Options(s));
    const auto overridden = overrideOwner.Load();
    Check(overridden.config.collision_enabled && overridden.config.lean_clamp.skin == 20.0f &&
          overridden.config.lean_clamp.release_smoothing == 0.2f && overridden.config.collision_channel == 36,
          "explicit collision settings override defaults");
    const Scratch migrated = MakeScratch(L"collision-migrated");
    WriteBytes(migrated.defaults, "[Position]\r\nCollisionEnabled=false\r\nCollisionReleaseSmoothing=0.4\r\n");
    WriteBytes(migrated.folder + Fallout4HT::kLegacyConfigFileName, "[General]\r\nAutoEnable=true\r\n");
    cfg::ConfigOwner<Config> legacyOwner(Options(migrated));
    const auto legacy = legacyOwner.Load();
    Check(legacy.status == cfg::ConfigLoadStatus::Migrated && !legacy.config.collision_enabled &&
          legacy.config.lean_clamp.release_smoothing == 0.4f,
          "legacy imports inherit the new collision settings from Defaults.ini");
}

void TrueFreeLookStartsOff() {
    std::printf("true free look is off by default, and an old ads_mode line is not read\n");
    const Config defaults = Fallout4HT::MakeConfigTable().defaults();
    Check(!defaults.true_free_look, "TrueFreeLook defaults to false");

    // tracked was never free look, so the retired cycle's key maps onto nothing.
    const Scratch s = MakeScratch(L"ads-mode");
    WriteBytes(s.folder + Fallout4HT::kConfigFileName,
               "[CameraUnlock]\r\nConfigFormat=1\r\n[Position]\r\nads_mode=tracked\r\n");
    cfg::ConfigOwner<Config> owner(Options(s));
    const cfg::ConfigLoadResult<Config> loaded = owner.Load();
    Check(loaded.status == cfg::ConfigLoadStatus::Canonical, "a file holding ads_mode still loads as canonical");
    Check(!loaded.config.true_free_look, "ads_mode=tracked leaves true free look off");
}

void TogglesSaveTheirRowsOnly() {
    std::printf("each toggle's save writes its own rows and no other byte\n");
    const Scratch s = MakeScratch(L"saves");
    const std::wstring path = s.folder + Fallout4HT::kConfigFileName;
    {
        cfg::ConfigOwner<Config> owner(Options(s));
        owner.Load();
    }
    const std::string fresh = ReadBytes(path);

    cfg::ConfigOwner<Config> owner(Options(s));
    owner.Load();
    const cfg::ConfigSaveResult yaw = owner.Save([](Config& c) { c.world_space_yaw = false; });
    Check(yaw.status == cfg::ConfigSaveStatus::Saved, "the yaw save is Saved");
    Check(!yaw.log.empty(), "the log says WorldSpaceYaw no longer follows Defaults.ini");
    const std::string afterYaw = ReadBytes(path);
    const std::vector<std::string> yawLines = ChangedLines(fresh, afterYaw);
    Check(yawLines.size() == 1 && yawLines[0] == "WorldSpaceYaw=false", "only WorldSpaceYaw=default became false");

    const cfg::ConfigSaveResult mode = owner.Save([](Config& c) {
        c.rotation_enabled = true;
        c.position_enabled = false;
    });
    Check(mode.status == cfg::ConfigSaveStatus::Saved, "the mode save is Saved");
    const std::vector<std::string> modeLines = ChangedLines(afterYaw, ReadBytes(path));
    Check(modeLines.size() == 2 && modeLines[0] == "RotationEnabled=true" && modeLines[1] == "PositionEnabled=false",
          "a mode change writes both rows of the pair and nothing else");

    bool threw = false;
    try {
        owner.Save([](Config& c) { c.enable_on_startup = false; });
    } catch (const std::exception&) {
        threw = true;
    }
    Check(threw, "EnableOnStartup is not Writable: End never persists");

    const std::string beforeFreeLook = ReadBytes(path);
    const cfg::ConfigSaveResult freeLook = owner.Save([](Config& c) { c.true_free_look = true; });
    Check(freeLook.status == cfg::ConfigSaveStatus::Saved, "the true free look save is Saved");
    const std::vector<std::string> freeLookLines = ChangedLines(beforeFreeLook, ReadBytes(path));
    Check(freeLookLines.size() == 1 && freeLookLines[0] == "TrueFreeLook=true",
          "only TrueFreeLook=default became true");

    cfg::ConfigOwner<Config> again(Options(s));
    const cfg::ConfigLoadResult<Config> reread = again.Load();
    Check(reread.status == cfg::ConfigLoadStatus::Canonical, "the saved file reads back as canonical");
    Check(!reread.config.world_space_yaw, "the yaw choice survives a restart");
    Check(reread.config.rotation_enabled && !reread.config.position_enabled, "the mode survives a restart");
    Check(reread.config.true_free_look, "true free look survives a restart");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::strcmp(argv[1], "--render-config") == 0) {
            const std::string path = argv[2];
            WriteBytes(std::wstring(path.begin(), path.end()), FreshRender());
            std::printf("wrote %s\n", argv[2]);
            return 0;
        }

        std::printf("Fallout4HeadTracking config tests\n=================================\n");
        CommittedFileIsTheFreshRender();
        EveryHotkeyDefaultParses();
        FirstLaunchCreatesTheCommittedFile();
        CollisionSettingsFollowDefaultsAndKeepEngineUnits();
        TrueFreeLookStartsOff();
        TogglesSaveTheirRowsOnly();
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }

    if (g_failures == 0) {
        std::printf("All tests passed!\n");
        return 0;
    }
    std::printf("%d test(s) FAILED\n", g_failures);
    return 1;
}
