// SPDX-License-Identifier: MIT
//
// The config differential test. Every input is read three ways:
//
//   oracle     the published build's reader (oracle/), the dev pre-release at fbefff4, with
//              its Mod::LoadConfig
//   import     the frozen reader in src/legacy_config/
//   migration  the config owner importing the input, as HeadTracking.ini, into a new
//              CameraUnlock.ini beside it, then the canonical reader and table on the result,
//              and the startup code of this build
//
// Comparison 1, oracle against import, finds what a player updating from the published build
// sees change that the conversion did not cause. Every difference it may find is listed in
// kComparisonOneDifferences with the commit that made it; any other fails the test. The frozen
// reader is the published one byte for byte, and every core source both compile is the same
// bytes at the published pin, so the list is empty.
//
// Comparison 2, import against migration, is the proof for the migration: no difference but
// the approved ones, each of which the import must record as dropped. A sensitivity or an
// axis inversion the player set away from what the build shipped is dropped (pose_shaping);
// the shipped InvertX=true is the x negation at the engine boundary now, which
// camera_math_tests holds bit for bit. A hotkey code outside 0x01-0xFE imports as unbound
// (N1), and so does a code on a Ctrl, Shift or Alt key alone (N3), the action keeping its
// Ctrl+Shift chord. No default moved, so the no-file input has no difference either. The
// frozen reader clamps every number it reads into a range the canonical rows hold and replaces
// a value that is not finite, so no input is deferred and N2 never applies.
//
// The main runs read one scratch Defaults.ini, which the first owner creates with the built-in
// values, so an input whose values are the built-in ones migrates to `default` rows. A row the
// player never changed from the frozen default follows Defaults.ini: the import lists it in
// follows_defaults_ini, the tracking mode pair as one unit, and the test holds that list to the
// rows the frozen reader read at their defaults on every input. Every input also migrates over
// a Defaults.ini that differs from the built-in values on every row, where each untouched row is
// written `default` and takes that file's value and each changed row keeps the player's. The
// distinct migrated files are written beside the executable under migrated\, for
// lint-migrated.mjs to run core's canonical config lint over.
//
// Inputs: no file, an empty file, the HeadTracking.ini the dev pre-release shipped (its
// installer ZIP's plugins\HeadTracking.ini; its launcher manifest seeds nothing), each distinct
// version of that file committed up to it, the file that build writes at first launch when
// there is none (extracted once into inputs/ with --first-run), core's corpus over the shipped
// file, and the shipped file with all three hotkeys on each code from 0x01 to 0xFE.

#include "core/config.h"
#include "legacy_config/legacy_config.h"
#include "oracle_adapter.h"

#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/defaults_file.h"
#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/config/testing/ini_mutations.h"
#include "cameraunlock/input/key_binding_registration.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/tracking/tracking_mode.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

namespace legacy = Fallout4HT::legacy;
namespace cfg = cameraunlock::config;
using Fallout4HT::Config;
using cameraunlock::TrackingMode;
using cameraunlock::config::testing::GenerateIniMutations;
using cameraunlock::config::testing::IniMutation;
using cameraunlock::config::testing::MutationKey;
using cameraunlock::input::KeyModifiers;

int g_failures = 0;

void Fail(const std::string& input, const std::string& what) {
    if (g_failures < 50) std::printf("FAIL [%s]: %s\n", input.c_str(), what.c_str());
    ++g_failures;
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

std::wstring Widen(const std::string& s) {
    return std::wstring(s.begin(), s.end());
}

std::string Narrow(const std::wstring& path) {
    const int size = WideCharToMultiByte(CP_ACP, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size), 'x');
    WideCharToMultiByte(CP_ACP, 0, path.c_str(), -1, out.data(), size, nullptr, nullptr);
    out.resize(static_cast<size_t>(size) - 1);
    return out;
}

std::string ReadBytes(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + Narrow(path));
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteBytes(const std::wstring& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write " + Narrow(path));
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// Every file in the folder, name and bytes, for "the import changed nothing".
std::map<std::wstring, std::string> Snapshot(const std::wstring& dir) {
    std::map<std::wstring, std::string> files;
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot list the test folder");
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        files[data.cFileName] = ReadBytes(dir + L"\\" + data.cFileName);
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return files;
}

void EmptyFolder(const std::wstring& dir) {
    for (const auto& [name, bytes] : Snapshot(dir)) {
        const std::wstring path = dir + L"\\" + name;
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (!DeleteFileW(path.c_str())) throw std::runtime_error("cannot empty the test folder");
    }
}

std::wstring MakeFolder(const std::wstring& parent, const wchar_t* name) {
    const std::wstring dir = parent + L"\\" + name;
    if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        throw std::runtime_error("cannot create the test folder");
    }
    EmptyFolder(dir);
    return dir;
}

// ---------------------------------------------------------------------------
// Hotkeys: what each build puts on its poller
// ---------------------------------------------------------------------------

enum class Action { Toggle, CycleMode, YawMode, TrackerSource };

const char* ActionName(Action a) {
    switch (a) {
        case Action::Toggle: return "toggle";
        case Action::CycleMode: return "cycle mode";
        case Action::YawMode: return "yaw mode";
        case Action::TrackerSource: return "tracker source";
    }
    throw std::logic_error("action");
}

// One key the poller watches for an action, and the modifiers it fires with: kNav is
// NavGuarded (not while Ctrl and Shift are both held), kChord is ChordGuarded (while both are
// held).
struct Registration {
    Action action;
    int vk;
    unsigned modifiers;

    bool operator<(const Registration& o) const {
        return std::tie(action, vk, modifiers) < std::tie(o.action, o.vk, o.modifiers);
    }
    bool operator==(const Registration& o) const {
        return action == o.action && vk == o.vk && modifiers == o.modifiers;
    }
    bool operator!=(const Registration& o) const { return !(*this == o); }
};

constexpr unsigned kNav = static_cast<unsigned>(KeyModifiers::kNone);
constexpr unsigned kChord = static_cast<unsigned>(KeyModifiers::kCtrl | KeyModifiers::kShift);

std::string Describe(const std::vector<Registration>& regs) {
    std::string out;
    for (const Registration& r : regs) {
        char buf[80];
        std::snprintf(buf, sizeof(buf), "%s%s:%s0x%02X", out.empty() ? "" : " ", ActionName(r.action),
                      r.modifiers == kChord ? "Ctrl+Shift+" : "", static_cast<unsigned>(r.vk));
        out += buf;
    }
    return out;
}

// Hotkeys::Start at fbefff4 (src/core/hotkeys.cpp), with the poller calls recorded. The chords
// were registered unconditionally, and the diagnostic keys are compiled out of a release build.
template <class C>
std::vector<Registration> PublishedHotkeys(const C& c) {
    std::vector<Registration> regs = {
        {Action::Toggle, c.toggleKey, kNav},
        {Action::CycleMode, c.positionToggleKey, kNav},
        {Action::YawMode, c.yawModeKey, kNav},
        {Action::Toggle, 'Y', kChord},
        {Action::CycleMode, 'G', kChord},
        {Action::YawMode, 'H', kChord},
        {Action::TrackerSource, 'U', kChord},
    };
    std::sort(regs.begin(), regs.end());
    return regs;
}

uint32_t Bits(float f) {
    uint32_t b;
    std::memcpy(&b, &f, sizeof(b));
    return b;
}

// Every field of the two Configs, floats bit for bit. Mod::Initialize and ConfigureSession
// take the whole of the startup state from these fields, and Hotkeys::Start the whole of the
// bindings, so equal fields are an equal start.
template <class A, class B>
std::vector<std::string> FieldDifferences(const A& a, const B& b) {
    std::vector<std::string> out;
    const auto check = [&out](bool same, const char* name) {
        if (!same) out.push_back(name);
    };
#define SAME(f) check(a.f == b.f, #f)
#define SAME_BITS(f) check(Bits(a.f) == Bits(b.f), #f)
    SAME(udpPort);
    SAME_BITS(yawMultiplier);
    SAME_BITS(pitchMultiplier);
    SAME_BITS(rollMultiplier);
    SAME_BITS(localSmoothing);
    SAME_BITS(remoteSmoothing);
    SAME(toggleKey);
    SAME(positionToggleKey);
    SAME(yawModeKey);
    SAME_BITS(positionSensitivityX);
    SAME_BITS(positionSensitivityY);
    SAME_BITS(positionSensitivityZ);
    SAME_BITS(positionLimitX);
    SAME_BITS(positionLimitY);
    SAME_BITS(positionLimitZ);
    SAME_BITS(positionLimitZBack);
    SAME(positionInvertX);
    SAME(positionInvertY);
    SAME(positionInvertZ);
    SAME(positionEnabled);
    SAME(autoEnable);
    SAME(showNotifications);
    SAME(worldSpaceYaw);
#undef SAME
#undef SAME_BITS
    return out;
}

// ---------------------------------------------------------------------------
// Comparison 1: the published build against the frozen reader
// ---------------------------------------------------------------------------

// What a player updating from the published build sees change, and the commit that made each
// change. The changelog carries the same list. Empty: the frozen reader and every source it
// compiles are the published build's bytes.
struct ListedDifference {
    const char* id;
    const char* commit;
    const char* what;
    int seen = 0;
};

std::vector<ListedDifference> kComparisonOneDifferences;

struct OracleRun {
    oracle_api::LoadStatus status = oracle_api::LoadStatus::Read;
    oracle_api::Config cfg;
};

struct ImportRun {
    legacy::ReadStatus status = legacy::ReadStatus::Read;
    legacy::Config cfg;
};

bool SameStatus(oracle_api::LoadStatus o, legacy::ReadStatus i) {
    switch (o) {
        case oracle_api::LoadStatus::Read: return i == legacy::ReadStatus::Read;
        case oracle_api::LoadStatus::Created: return i == legacy::ReadStatus::Absent;
    }
    throw std::logic_error("status");
}

void CompareOracleWithImport(const std::string& name, const OracleRun& o, const ImportRun& i) {
    if (!SameStatus(o.status, i.status)) {
        Fail(name, "comparison 1: the published build and the import do not agree on whether the file was read");
        return;
    }
    for (const std::string& field : FieldDifferences(o.cfg, i.cfg)) {
        Fail(name, "comparison 1: " + field + " differs from the published build with no listed reason");
    }
    const std::vector<Registration> expected = PublishedHotkeys(o.cfg);
    const std::vector<Registration> actual = PublishedHotkeys(i.cfg);
    if (expected != actual) {
        Fail(name, "comparison 1: hotkeys " + Describe(actual) + " against the published build's " + Describe(expected));
    }
}

// ---------------------------------------------------------------------------
// The keys the frozen reader reads, and the corpus descriptors
// ---------------------------------------------------------------------------

// One valid value other than the shipped one, and one value past each bound the reader clamps
// or refuses, for every key legacy::ReadKeys lists.
std::vector<MutationKey> CorpusKeys() {
    const auto boolean = [](const char* s, const char* k, const char* alternate) {
        return MutationKey{s, k, alternate, {}, false, {}};
    };
    const auto multiplier = [](const char* k) { return MutationKey{"Sensitivity", k, "0.5", {"0.05", "6"}, false, {}}; };
    const auto smooth = [](const char* k) { return MutationKey{"Sensitivity", k, "0.3", {"-0.5", "1.5"}, false, {}}; };
    const auto sens = [](const char* k) { return MutationKey{"Position", k, "0.5", {"0.05", "11"}, false, {}}; };
    const auto limit = [](const char* k) { return MutationKey{"Position", k, "0.25", {"0.001", "3"}, false, {}}; };
    const auto hotkey = [](const char* k, const char* alt) {
        return MutationKey{"Hotkeys", k, alt, {"0xFF", "0x100", "-1"}, true, {}};
    };
    return {
        MutationKey{"Network", "UDPPort", "5000", {"80", "65536"}, false, {}},
        multiplier("YawMultiplier"),
        multiplier("PitchMultiplier"),
        MutationKey{"Sensitivity", "RollMultiplier", "0.5", {"-1", "3"}, false, {}},
        smooth("LocalSmoothing"),
        smooth("RemoteSmoothing"),
        hotkey("ToggleKey", "0x70"),
        hotkey("PositionToggleKey", "0x71"),
        hotkey("YawModeKey", "0x72"),
        sens("SensitivityX"),
        sens("SensitivityY"),
        sens("SensitivityZ"),
        limit("LimitX"),
        limit("LimitY"),
        limit("LimitZ"),
        limit("LimitZBack"),
        boolean("Position", "InvertX", "false"),
        boolean("Position", "InvertY", "true"),
        boolean("Position", "InvertZ", "true"),
        boolean("Position", "Enabled", "false"),
        boolean("General", "AutoEnable", "false"),
        boolean("General", "ShowNotifications", "false"),
        boolean("General", "WorldSpaceYaw", "false"),
    };
}

// ---------------------------------------------------------------------------
// Comparison 2: the frozen reader against the migration
// ---------------------------------------------------------------------------

// What the mod starts with. Pose shaping is not here: the migrated build applies none, which
// CheckNoShaping holds it to, and CheckPoseShaping holds the import to listing every value it
// leaves out.
struct Startup {
    int port = 0;
    bool enabled = false;
    TrackingMode mode = TrackingMode::RotationAndPosition;
    bool world_yaw = false;
    bool notifications = false;
    uint32_t local_smoothing = 0;
    uint32_t remote_smoothing = 0;
    uint32_t limit_x = 0;
    uint32_t limit_y = 0;
    uint32_t limit_y_down = 0;
    uint32_t limit_z = 0;
    uint32_t limit_z_back = 0;
    std::vector<Registration> hotkeys;
};

const cfg::DroppedValue* FindDrop(const std::vector<cfg::DroppedValue>& dropped, cfg::DropRule rule,
                                  const char* section, const char* key) {
    for (const cfg::DroppedValue& d : dropped) {
        if (d.rule == rule && d.section == section && d.key == key) return &d;
    }
    return nullptr;
}

bool IsModifierCode(int vk) {
    return (vk >= 0x10 && vk <= 0x12) || (vk >= 0xA0 && vk <= 0xA5);
}

// A hotkey code outside 0x01-0xFE imports as unbound (N1), and so does a code on a Ctrl, Shift or
// Alt key alone (N3); only those are dropped. Code 0 never fired on the published build's poller
// and imports as unbound, unrecorded.
bool KeyKept(const std::string& name, int vk, const char* key, const std::vector<cfg::DroppedValue>& dropped) {
    const bool outOfRange = vk != 0 && (vk < 0x01 || vk > 0xFE);
    if (outOfRange != (FindDrop(dropped, cfg::DropRule::KeyCodeOutOfRange, "Hotkeys", key) != nullptr)) {
        Fail(name, std::string("[Hotkeys] ") + key + " dropped as out of range does not match its code");
    }
    const bool modifier = IsModifierCode(vk);
    if (modifier != (FindDrop(dropped, cfg::DropRule::ModifierKey, "Hotkeys", key) != nullptr)) {
        Fail(name, std::string("[Hotkeys] ") + key + " dropped as a modifier key does not match its code");
    }
    return vk != 0 && !outOfRange && !modifier;
}

// Mod::Initialize and ConfigureSession as the published build and commit A ran them: [Position]
// Enabled chose between the first two modes, BuildPositionSettings copied LimitY into both
// vertical limits, and Hotkeys::Start registered the keys, less a code N1 unbinds.
Startup FromImport(const std::string& name, const legacy::Config& c, const std::vector<cfg::DroppedValue>& dropped) {
    Startup s;
    s.port = c.udpPort;
    s.enabled = c.autoEnable;
    s.mode = c.positionEnabled ? TrackingMode::RotationAndPosition : TrackingMode::RotationOnly;
    s.world_yaw = c.worldSpaceYaw;
    s.notifications = c.showNotifications;
    s.local_smoothing = Bits(c.localSmoothing);
    s.remote_smoothing = Bits(c.remoteSmoothing);
    s.limit_x = Bits(c.positionLimitX);
    s.limit_y = Bits(c.positionLimitY);
    s.limit_y_down = Bits(c.positionLimitY);
    s.limit_z = Bits(c.positionLimitZ);
    s.limit_z_back = Bits(c.positionLimitZBack);
    const bool toggleKept = KeyKept(name, c.toggleKey, "ToggleKey", dropped);
    const bool cycleKept = KeyKept(name, c.positionToggleKey, "PositionToggleKey", dropped);
    const bool yawKept = KeyKept(name, c.yawModeKey, "YawModeKey", dropped);
    for (const Registration& r : PublishedHotkeys(c)) {
        const bool kept = r.modifiers == kChord || (r.action == Action::Toggle && toggleKept) ||
                          (r.action == Action::CycleMode && cycleKept) || (r.action == Action::YawMode && yawKept);
        if (kept) s.hotkeys.push_back(r);
    }
    return s;
}

// This build: Mod::Initialize, ConfigureSession, and the lists Hotkeys::Start registers.
Startup FromMigration(const Config& c) {
    Startup s;
    s.port = c.udp_port;
    s.enabled = c.enable_on_startup;
    s.mode = cameraunlock::DecodeTrackingMode(c.rotation_enabled, c.position_enabled).value();
    s.world_yaw = c.world_space_yaw;
    s.notifications = c.show_notifications;
    s.local_smoothing = Bits(c.local_smoothing);
    s.remote_smoothing = Bits(c.remote_smoothing);
    s.limit_x = Bits(c.position.limit_x);
    s.limit_y = Bits(c.position.limit_y);
    s.limit_y_down = Bits(c.position.limit_y_down);
    s.limit_z = Bits(c.position.limit_z);
    s.limit_z_back = Bits(c.position.limit_z_back);
    const std::pair<Action, const std::string*> lists[] = {
        {Action::Toggle, &c.toggle_key_name},
        {Action::CycleMode, &c.cycle_tracking_mode_key_name},
        {Action::YawMode, &c.yaw_mode_key_name},
        {Action::TrackerSource, &c.cycle_tracker_source_key_name},
    };
    for (const auto& [action, list] : lists) {
        const cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(*list);
        if (!parsed.ok()) throw std::logic_error("a migrated hotkey list does not parse: " + *list);
        for (const cameraunlock::input::KeyBinding& b : parsed.bindings) {
            s.hotkeys.push_back({action, b.vk, static_cast<unsigned>(b.modifiers)});
        }
    }
    std::sort(s.hotkeys.begin(), s.hotkeys.end());
    return s;
}

std::vector<std::string> StartupDifferences(const Startup& a, const Startup& b) {
    std::vector<std::string> out;
#define SAME(f) \
    if (a.f != b.f) out.push_back(#f)
    SAME(port);
    SAME(enabled);
    SAME(mode);
    SAME(world_yaw);
    SAME(notifications);
    SAME(local_smoothing);
    SAME(remote_smoothing);
    SAME(limit_x);
    SAME(limit_y);
    SAME(limit_y_down);
    SAME(limit_z);
    SAME(limit_z_back);
#undef SAME
    if (a.hotkeys != b.hotkeys) out.push_back("hotkeys " + Describe(a.hotkeys) + " against " + Describe(b.hotkeys));
    return out;
}

// The runtime config applies no pose shaping of its own: the position processor gets it
// whole, and the rotation processor's sensitivity is never set, so both must be identity, and
// the position smoothing must be the smoothing rows' values.
void CheckNoShaping(const std::string& name, const Config& c) {
    const cameraunlock::PositionSettings& p = c.position;
    if (p.sensitivity_x != 1.0f || p.sensitivity_y != 1.0f || p.sensitivity_z != 1.0f || p.invert_x || p.invert_y ||
        p.invert_z) {
        Fail(name, "the migrated config shapes the position");
    }
    if (Bits(p.local_smoothing) != Bits(c.local_smoothing) || Bits(p.remote_smoothing) != Bits(c.remote_smoothing)) {
        Fail(name, "the position smoothing is not the smoothing rows' values");
    }
}

// Every sensitivity and inversion the frozen reader read is listed in its place, folded where
// it holds what the build shipped and dropped as PoseShaping where it does not. Returns how
// many were dropped.
int CheckPoseShaping(const std::string& name, const legacy::Config& c, const cfg::ImportResult& result) {
    struct Read {
        const char* section;
        const char* key;
        bool shipped;
    };
    const Read reads[] = {
        {"Sensitivity", "YawMultiplier", c.yawMultiplier == legacy::kDefaultMultiplier},
        {"Sensitivity", "PitchMultiplier", c.pitchMultiplier == legacy::kDefaultMultiplier},
        {"Sensitivity", "RollMultiplier", c.rollMultiplier == legacy::kDefaultMultiplier},
        {"Position", "SensitivityX", c.positionSensitivityX == legacy::kDefaultPositionSensitivity},
        {"Position", "SensitivityY", c.positionSensitivityY == legacy::kDefaultPositionSensitivity},
        {"Position", "SensitivityZ", c.positionSensitivityZ == legacy::kDefaultPositionSensitivity},
        {"Position", "InvertX", c.positionInvertX == legacy::kDefaultPositionInvertX},
        {"Position", "InvertY", c.positionInvertY == legacy::kDefaultPositionInvertY},
        {"Position", "InvertZ", c.positionInvertZ == legacy::kDefaultPositionInvertZ},
    };
    if (result.pose_shaping.size() != std::size(reads)) {
        Fail(name, "the import lists " + std::to_string(result.pose_shaping.size()) + " pose-shaping values, not 9");
        return 0;
    }
    int dropped = 0;
    for (size_t k = 0; k < std::size(reads); ++k) {
        const cfg::PoseShapingValue& v = result.pose_shaping[k];
        const std::string label = std::string("[") + reads[k].section + "] " + reads[k].key;
        if (v.section != reads[k].section || v.key != reads[k].key) Fail(name, label + " is not listed in its place");
        if (v.folded != reads[k].shipped) Fail(name, label + " is " + (v.folded ? "folded" : "dropped") + " wrongly");
        const bool listed = FindDrop(result.dropped, cfg::DropRule::PoseShaping, reads[k].section, reads[k].key) != nullptr;
        if (listed == reads[k].shipped) {
            Fail(name, label + (listed ? " is dropped at its shipped value" : " is changed and not dropped"));
        }
        if (!reads[k].shipped) ++dropped;
    }
    return dropped;
}

// Every drop the import recorded is by one of the approved rules this map applies.
void CheckDropRules(const std::string& name, const cfg::ImportResult& result) {
    for (const cfg::DroppedValue& d : result.dropped) {
        const bool approved = d.rule == cfg::DropRule::PoseShaping || d.rule == cfg::DropRule::KeyCodeOutOfRange ||
                              d.rule == cfg::DropRule::ModifierKey;
        if (!approved) Fail(name, "the import drops [" + d.section + "] " + d.key + " by a rule this map never applies");
    }
}

// ---------------------------------------------------------------------------
// The rows that follow Defaults.ini
// ---------------------------------------------------------------------------

using Concept = cfg::schema::Concept;

// Every row of the table that follows Defaults.ini: every global concept it binds, none of
// them PerGame.
const std::set<Concept>& AllRows() {
    static const std::set<Concept> all = {
        Concept::UdpPort,         Concept::EnableOnStartup,      Concept::WorldSpaceYaw,
        Concept::RotationEnabled, Concept::PositionEnabled,      Concept::LocalSmoothing,
        Concept::RemoteSmoothing, Concept::PositionLimitX,       Concept::PositionLimitY,
        Concept::PositionLimitYDown, Concept::PositionLimitZ,    Concept::PositionLimitZBack,
        Concept::ToggleKey,       Concept::CycleTrackingModeKey, Concept::YawModeKey,
    };
    return all;
}

// The rows the player never changed: every value the frozen reader gave the row is its
// default. LimitY stood for both vertical bounds, [Position] Enabled for the tracking mode pair,
// and the chord beside each hotkey code was fixed.
std::set<Concept> UntouchedRows(const legacy::Config& c) {
    const legacy::Config d;
    std::set<Concept> rows;
    const auto untouched = [&rows](bool same, std::initializer_list<Concept> ids) {
        if (same) rows.insert(ids);
    };
    untouched(c.udpPort == d.udpPort, {Concept::UdpPort});
    untouched(c.autoEnable == d.autoEnable, {Concept::EnableOnStartup});
    untouched(c.worldSpaceYaw == d.worldSpaceYaw, {Concept::WorldSpaceYaw});
    untouched(c.positionEnabled == d.positionEnabled, {Concept::RotationEnabled, Concept::PositionEnabled});
    untouched(Bits(c.localSmoothing) == Bits(d.localSmoothing), {Concept::LocalSmoothing});
    untouched(Bits(c.remoteSmoothing) == Bits(d.remoteSmoothing), {Concept::RemoteSmoothing});
    untouched(Bits(c.positionLimitX) == Bits(d.positionLimitX), {Concept::PositionLimitX});
    untouched(Bits(c.positionLimitY) == Bits(d.positionLimitY), {Concept::PositionLimitY, Concept::PositionLimitYDown});
    untouched(Bits(c.positionLimitZ) == Bits(d.positionLimitZ), {Concept::PositionLimitZ});
    untouched(Bits(c.positionLimitZBack) == Bits(d.positionLimitZBack), {Concept::PositionLimitZBack});
    untouched(c.toggleKey == d.toggleKey, {Concept::ToggleKey});
    untouched(c.positionToggleKey == d.positionToggleKey, {Concept::CycleTrackingModeKey});
    untouched(c.yawModeKey == d.yawModeKey, {Concept::YawModeKey});
    return rows;
}

std::string Names(const std::set<Concept>& rows) {
    std::string text;
    for (const Concept row : rows) {
        text += (text.empty() ? "" : ", ") + std::string(cfg::schema::kConcepts[static_cast<size_t>(row)].name);
    }
    return text.empty() ? "none" : text;
}

// The start `imported` gives, with each row in `follows` as `defaults` has it.
Startup OverDefaults(Startup imported, const std::set<Concept>& follows, const Startup& defaults) {
    const auto take = [&follows](Concept row, auto& field, const auto& value) {
        if (follows.count(row)) field = value;
    };
    take(Concept::UdpPort, imported.port, defaults.port);
    take(Concept::EnableOnStartup, imported.enabled, defaults.enabled);
    take(Concept::WorldSpaceYaw, imported.world_yaw, defaults.world_yaw);
    if (follows.count(Concept::RotationEnabled) || follows.count(Concept::PositionEnabled)) imported.mode = defaults.mode;
    take(Concept::LocalSmoothing, imported.local_smoothing, defaults.local_smoothing);
    take(Concept::RemoteSmoothing, imported.remote_smoothing, defaults.remote_smoothing);
    take(Concept::PositionLimitX, imported.limit_x, defaults.limit_x);
    take(Concept::PositionLimitY, imported.limit_y, defaults.limit_y);
    take(Concept::PositionLimitYDown, imported.limit_y_down, defaults.limit_y_down);
    take(Concept::PositionLimitZ, imported.limit_z, defaults.limit_z);
    take(Concept::PositionLimitZBack, imported.limit_z_back, defaults.limit_z_back);
    const std::pair<Concept, Action> hotkeys[] = {
        {Concept::ToggleKey, Action::Toggle},
        {Concept::CycleTrackingModeKey, Action::CycleMode},
        {Concept::YawModeKey, Action::YawMode},
    };
    for (const auto& [row, action] : hotkeys) {
        if (!follows.count(row)) continue;
        const Action a = action;
        imported.hotkeys.erase(std::remove_if(imported.hotkeys.begin(), imported.hotkeys.end(),
                                              [a](const Registration& r) { return r.action == a; }),
                               imported.hotkeys.end());
        for (const Registration& r : defaults.hotkeys) {
            if (r.action == a) imported.hotkeys.push_back(r);
        }
    }
    std::sort(imported.hotkeys.begin(), imported.hotkeys.end());
    return imported;
}

// A Defaults.ini that differs from the built-in values on every row the table binds, the
// tracking mode on both halves.
const char kSkewedDefaults[] =
    "[CameraUnlock]\r\nConfigFormat=1\r\n\r\n"
    "[Network]\r\nUdpPort=5353\r\n\r\n"
    "[General]\r\nEnableOnStartup=false\r\nWorldSpaceYaw=false\r\nRotationEnabled=false\r\n\r\n"
    "[Smoothing]\r\nLocalSmoothing=0.5\r\nRemoteSmoothing=0.45\r\n\r\n"
    "[Position]\r\nPositionEnabled=true\r\nPositionLimitX=0.55\r\nPositionLimitY=0.45\r\n"
    "PositionLimitYDown=0.35\r\nPositionLimitZ=0.65\r\nPositionLimitZBack=0.25\r\n\r\n"
    "[Hotkeys]\r\nToggleKey=F8\r\nCycleTrackingModeKey=F9\r\nYawModeKey=F10\r\n";

struct MigrationTally {
    std::string committed;
    std::wstring defaults;
    std::wstring skewed_defaults;
    // What a session with no legacy file starts with over kSkewedDefaults.
    Startup skewed;
    std::set<std::string> migrated;
    int created = 0;
    int converted = 0;
    int with_pose_shaping_dropped = 0;
    int with_n1 = 0;
    int with_n3 = 0;
    int with_row_changed = 0;
    int with_mode_changed = 0;
};

cfg::ConfigOwnerOptions<Config> Options(const std::wstring& dir, const std::wstring& defaults) {
    return Fallout4HT::MakeConfigOwnerOptions(dir + L"\\", cfg::DefaultsFile::At(defaults));
}

FILETIME WriteTime(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        throw std::runtime_error("cannot read a test file's attributes");
    }
    return data.ftLastWriteTime;
}

bool SameTime(const FILETIME& a, const FILETIME& b) {
    return a.dwLowDateTime == b.dwLowDateTime && a.dwHighDateTime == b.dwHighDateTime;
}

// ---------------------------------------------------------------------------
// The run
// ---------------------------------------------------------------------------

// Each input is read in folders of its own. With one file per reader rewritten for every
// input, three runs of this test failed on 3, 17 and 0 inputs, each time a reader getting
// values another input held: both readers go through GetPrivateProfileString, and a path it
// has not seen before has not failed that way since.
struct Folders {
    std::wstring root;
    std::wstring oracle;
    std::wstring import;
    std::wstring migration;
    std::wstring read_only;
    std::wstring skewed;
};

Folders NextFolders(const std::wstring& root) {
    static int n = 0;
    const std::wstring dir = MakeFolder(root, std::to_wstring(n++).c_str());
    return {dir,
            MakeFolder(dir, L"oracle"),
            MakeFolder(dir, L"import"),
            MakeFolder(dir, L"migration"),
            MakeFolder(dir, L"read-only"),
            MakeFolder(dir, L"skewed")};
}

void RemoveFolders(const Folders& f) {
    for (const std::wstring& dir : {f.oracle, f.import, f.migration, f.read_only, f.skewed, f.root}) {
        EmptyFolder(dir);
        if (!RemoveDirectoryW(dir.c_str())) throw std::runtime_error("cannot remove a test folder");
    }
}

const wchar_t kIniName[] = L"HeadTracking.ini";

// The owner's load on the legacy file `bytes` in `dir`, read-only when asked, with everything
// the migration must leave as it was checked afterwards. Returns the migrated bytes, or
// nothing when no file was created.
struct Migration {
    cfg::ConfigLoadResult<Config> loaded;
    std::optional<std::string> bytes;
};

Migration Migrate(const std::string& name, const std::wstring& dir, const std::optional<std::string>& legacyBytes,
                  bool readOnly, const std::wstring& defaults) {
    const std::wstring legacyPath = dir + L"\\" + kIniName;
    const std::wstring path = dir + L"\\" + Fallout4HT::kConfigFileName;
    FILETIME before{};
    if (legacyBytes) {
        WriteBytes(legacyPath, *legacyBytes);
        if (readOnly) SetFileAttributesW(legacyPath.c_str(), FILE_ATTRIBUTE_READONLY);
        before = WriteTime(legacyPath);
    }

    Migration m{};
    {
        cfg::ConfigOwner<Config> owner(Options(dir, defaults));
        m.loaded = owner.Load();
    }

    std::map<std::wstring, std::string> expected;
    if (legacyBytes) {
        expected[kIniName] = *legacyBytes;
        if (!SameTime(WriteTime(legacyPath), before)) Fail(name, "the legacy file's write time changed");
        const DWORD attributes = GetFileAttributesW(legacyPath.c_str());
        if (((attributes & FILE_ATTRIBUTE_READONLY) != 0) != readOnly) Fail(name, "the legacy file's read-only attribute changed");
    }
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        m.bytes = ReadBytes(path);
        expected[Fallout4HT::kConfigFileName] = *m.bytes;
    }
    if (Snapshot(dir) != expected) Fail(name, "the folder holds files other than the legacy file and CameraUnlock.ini");
    return m;
}

// The migrated file loaded again over the same Defaults.ini: it reads as canonical with
// nothing to report, gives the same start, imports nothing and changes neither file.
void CheckSecondLoad(const std::string& name, const std::wstring& dir, const Migration& first,
                     const std::optional<std::string>& legacyBytes, const MigrationTally& tally) {
    const auto before = Snapshot(dir);
    cfg::ConfigOwner<Config> owner(Options(dir, tally.defaults));
    const cfg::ConfigLoadResult<Config> again = owner.Load();
    if (again.status != cfg::ConfigLoadStatus::Canonical) Fail(name, "the second load is not Canonical");
    if (!again.diagnostics.empty()) Fail(name, "the migrated file draws diagnostics");
    if (!StartupDifferences(FromMigration(first.loaded.config), FromMigration(again.config)).empty()) {
        Fail(name, "the second load starts differently from the first");
    }
    if (Snapshot(dir) != before) Fail(name, "the second load changed a file");
    const bool saysLegacyUnread = std::any_of(again.log.begin(), again.log.end(), [](const std::string& line) {
        return line.find("is left as it was and is not read") != std::string::npos;
    });
    if (legacyBytes.has_value() != saysLegacyUnread) {
        Fail(name, "the second load's log does not say whether the legacy file was left unread");
    }
}

void MigrateInput(const Folders& f, const std::string& name, const std::optional<std::string>& bytes,
                  const ImportRun& i, const cfg::ImportResult* result, MigrationTally& tally) {
    using cfg::ConfigLoadStatus;
    const Migration m = Migrate(name, f.migration, bytes, false, tally.defaults);
    CheckNoShaping(name, m.loaded.config);

    if (!bytes) {
        ++tally.created;
        if (m.loaded.status != ConfigLoadStatus::Created) Fail(name, "no file is not Created");
        if (m.bytes != tally.committed) Fail(name, "the created file is not HeadTracking.ini as committed");
        for (const std::string& d : StartupDifferences(FromImport(name, i.cfg, {}), FromMigration(m.loaded.config))) {
            Fail(name, "comparison 2: " + d);
        }
        CheckSecondLoad(name, f.migration, m, bytes, tally);
        return;
    }

    if (CheckPoseShaping(name, i.cfg, *result) > 0) ++tally.with_pose_shaping_dropped;
    CheckDropRules(name, *result);
    if (std::any_of(result->dropped.begin(), result->dropped.end(),
                    [](const cfg::DroppedValue& d) { return d.rule == cfg::DropRule::KeyCodeOutOfRange; })) {
        ++tally.with_n1;
    }
    if (std::any_of(result->dropped.begin(), result->dropped.end(),
                    [](const cfg::DroppedValue& d) { return d.rule == cfg::DropRule::ModifierKey; })) {
        ++tally.with_n3;
    }

    const Startup imported = FromImport(name, i.cfg, result->dropped);
    for (const std::string& d : StartupDifferences(imported, FromMigration(m.loaded.config))) {
        Fail(name, "comparison 2: " + d);
    }

    // A row the player never changed from the frozen default is left to Defaults.ini, the
    // tracking mode pair as one unit, and every other row keeps the player's value.
    const std::set<Concept> follows(result->follows_defaults_ini.begin(), result->follows_defaults_ini.end());
    if (follows.size() != result->follows_defaults_ini.size()) Fail(name, "follows_defaults_ini names a row twice");
    const std::set<Concept> untouched = UntouchedRows(i.cfg);
    if (follows != untouched) {
        Fail(name, "the import leaves " + Names(follows) + " to Defaults.ini, and the player never changed " +
                       Names(untouched));
    }
    // Every file a build shipped or wrote, and the empty one, holds only the frozen defaults.
    const bool unedited = name == "empty file" || name == "shipped, dev" || name == "first run, dev" ||
                          name.rfind("committed, ", 0) == 0;
    if (unedited && untouched != AllRows()) Fail(name, "a file a build wrote leaves only " + Names(untouched) + " to Defaults.ini");
    if (untouched != AllRows()) ++tally.with_row_changed;
    if (!untouched.count(Concept::RotationEnabled)) ++tally.with_mode_changed;

    // Over a Defaults.ini that differs everywhere, the untouched rows are written `default` and
    // take its values, and a changed row keeps the player's value (written `default` only where
    // it is the one that file gives).
    const Migration sk = Migrate(name, f.skewed, bytes, false, tally.skewed_defaults);
    if (sk.loaded.status != ConfigLoadStatus::Migrated || !sk.bytes) {
        Fail(name, "the migration over the skewed Defaults.ini is not Migrated");
    } else {
        for (const std::string& d :
             StartupDifferences(OverDefaults(imported, follows, tally.skewed), FromMigration(sk.loaded.config))) {
            Fail(name, "over the skewed Defaults.ini: " + d);
        }
        for (const Concept row : follows) {
            const std::string key = cfg::schema::kConcepts[static_cast<size_t>(row)].key;
            if (sk.bytes->find("\r\n" + key + "=default\r\n") == std::string::npos) {
                Fail(name, "over the skewed Defaults.ini, " + key + " is not written default");
            }
        }
    }

    ++tally.converted;
    if (m.loaded.status != ConfigLoadStatus::Migrated) {
        Fail(name, std::string("the migration is ") + cfg::ConfigLoadStatusName(m.loaded.status) + ": " + m.loaded.reason);
        return;
    }
    tally.migrated.insert(*m.bytes);

    const Migration ro = Migrate(name, f.read_only, bytes, true, tally.defaults);
    if (ro.loaded.status != ConfigLoadStatus::Migrated || ro.bytes != m.bytes) {
        Fail(name, "a read-only legacy file does not import as a writable one does");
    }

    CheckSecondLoad(name, f.migration, m, bytes, tally);
}

void RunInput(const std::wstring& root, const std::string& name, const std::optional<std::string>& bytes,
              MigrationTally& tally) {
    const Folders f = NextFolders(root);
    OracleRun o;
    {
        const std::wstring path = f.oracle + L"\\" + kIniName;
        if (bytes) WriteBytes(path, *bytes);
        o.status = oracle_api::LoadOrCreate(Narrow(path).c_str(), o.cfg);
    }

    ImportRun i;
    std::optional<cfg::ImportResult> result;
    {
        const std::wstring path = f.import + L"\\" + kIniName;
        if (bytes) {
            WriteBytes(path, *bytes);
            SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY);
        }
        const auto before = Snapshot(f.import);
        i.status = legacy::Read(Narrow(path).c_str(), i.cfg);
        if (bytes) {
            Config mapped = Fallout4HT::MakeConfigTable().defaults();
            result = Fallout4HT::MakeLegacyImport().run(cfg::LegacyInput{path, Narrow(path), false}, mapped);
        }
        if (Snapshot(f.import) != before) Fail(name, "the import changed the folder it read from");
        if (!bytes && i.status != legacy::ReadStatus::Absent) Fail(name, "the import read a file that is not there");
    }

    CompareOracleWithImport(name, o, i);
    MigrateInput(f, name, bytes, i, result ? &*result : nullptr, tally);
    RemoveFolders(f);
}

// A legacy file another program holds open with no sharing. The published build found the
// file, so it wrote nothing, and GetPrivateProfileString read nothing from it, so it ran on its
// defaults. The owner defers the import on its own defaults, which start the same, creates
// nothing and saves nothing that session.
void TestUnopenableFile(const std::wstring& root, const std::string& shipped, const MigrationTally& tally) {
    const std::string name = "a legacy file another program holds open with no sharing";
    const Folders f = NextFolders(root);
    OracleRun o;
    ImportRun i;
    std::optional<cfg::ConfigLoadResult<Config>> loaded;
    for (const std::wstring& dir : {f.oracle, f.import, f.migration}) {
        const std::wstring path = dir + L"\\" + kIniName;
        WriteBytes(path, shipped);
        HANDLE held = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (held == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot hold the test file open");
        if (dir == f.oracle) {
            o.status = oracle_api::LoadOrCreate(Narrow(path).c_str(), o.cfg);
        } else if (dir == f.import) {
            i.status = legacy::Read(Narrow(path).c_str(), i.cfg);
        } else {
            cfg::ConfigOwner<Config> owner(Options(dir, tally.defaults));
            loaded.emplace(owner.Load());
            if (owner.Save([](Config& c) { c.world_space_yaw = false; }).status != cfg::ConfigSaveStatus::NotSaved) {
                Fail(name, "a deferred session saved");
            }
        }
        CloseHandle(held);
        if (ReadBytes(path) != shipped) Fail(name, "a build rewrote a file it could not open");
    }
    CompareOracleWithImport(name, o, i);
    if (loaded->status != cfg::ConfigLoadStatus::Deferred) {
        Fail(name, std::string("the owner's load is ") + cfg::ConfigLoadStatusName(loaded->status) + ", not Deferred");
    }
    for (const std::string& d : StartupDifferences(FromImport(name, i.cfg, {}), FromMigration(loaded->config))) {
        Fail(name, "comparison 2: " + d);
    }
    if (Snapshot(f.migration) != std::map<std::wstring, std::string>{{kIniName, shipped}}) {
        Fail(name, "a deferred import created a file or changed the legacy one");
    }
    RemoveFolders(f);
}

// Registration compares the two builds by key and modifiers, which holds only while a binding
// with no modifiers fires as the old build's NavGuarded did (not while Ctrl and Shift are both
// held) and a Ctrl+Shift binding as its ChordGuarded did (while both are held). Alt changes
// neither.
void TestRegistrationModel() {
    using cameraunlock::input::detail::BindingFires;
    for (unsigned held = 0; held < 8; ++held) {
        const auto mods = static_cast<KeyModifiers>(held);
        const bool chordHeld = cameraunlock::input::HasModifiers(mods, KeyModifiers::kCtrl | KeyModifiers::kShift);
        if (BindingFires(KeyModifiers::kNone, mods) != !chordHeld) {
            Fail("registration", "a key with no modifiers does not fire as NavGuarded did, held " + std::to_string(held));
        }
        if (BindingFires(KeyModifiers::kCtrl | KeyModifiers::kShift, mods) != chordHeld) {
            Fail("registration", "a Ctrl+Shift key does not fire as ChordGuarded did, held " + std::to_string(held));
        }
    }
}

std::string ReadInput(const std::string& file) {
    return ReadBytes(Widen(std::string(F4_DIFFERENTIAL_INPUTS) + "/" + file));
}

// `text` with the value of its one `key=` line replaced, the rest of that line included.
std::string WithValue(const std::string& text, const std::string& key, const std::string& value) {
    const size_t at = text.find("\n" + key + "=");
    if (at == std::string::npos || text.find("\n" + key + "=", at + 1) != std::string::npos) {
        throw std::logic_error("the shipped file does not hold exactly one " + key + " line");
    }
    const size_t start = at + 1 + key.size() + 1;
    const size_t end = text.find_first_of("\r\n", start);
    return text.substr(0, start) + value + text.substr(end);
}

// Every hotkey code the frozen reader can hand the poller in range, 0x01 to 0xFE, on all three
// hotkeys at once. The corpus tries one alternate code per hotkey; this is where the codes the
// key table has no name for, or names only as a modifier, are covered.
std::vector<std::pair<std::string, std::string>> EveryHotkeyCode(const std::string& shipped) {
    std::vector<std::pair<std::string, std::string>> inputs;
    for (int vk = 0x01; vk <= 0xFE; ++vk) {
        char code[8];
        std::snprintf(code, sizeof(code), "0x%02X", static_cast<unsigned>(vk));
        std::string bytes = shipped;
        for (const char* key : {"ToggleKey", "PositionToggleKey", "YawModeKey"}) bytes = WithValue(bytes, key, code);
        inputs.emplace_back(std::string("every hotkey ") + code, bytes);
    }
    return inputs;
}

void TestFrozenDefaults() {
    const oracle_api::Config o = oracle_api::Defaults();
    const legacy::Config l;
    for (const std::string& field : FieldDifferences(o, l)) {
        Fail("defaults", field + ": the frozen default differs from the published build's");
    }
}

// The published build's first-run output, which inputs/first-run-dev.ini holds.
std::string OracleFirstRun(const std::wstring& root) {
    const Folders f = NextFolders(root);
    const std::wstring path = f.oracle + L"\\" + kIniName;
    oracle_api::Config created;
    if (oracle_api::LoadOrCreate(Narrow(path).c_str(), created) != oracle_api::LoadStatus::Created) {
        throw std::logic_error("the oracle read a file in an empty folder");
    }
    std::string bytes = ReadBytes(path);
    RemoveFolders(f);
    return bytes;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        wchar_t temp[MAX_PATH];
        GetTempPathW(MAX_PATH, temp);
        const std::wstring root = std::wstring(temp) + L"fallout4-config-differential-" +
                                  std::to_wstring(GetCurrentProcessId());
        CreateDirectoryW(root.c_str(), nullptr);

        // `--first-run <path>` writes the published build's first-run output to <path> and runs
        // nothing else: how inputs/first-run-dev.ini was extracted.
        if (argc == 3 && std::strcmp(argv[1], "--first-run") == 0) {
            WriteBytes(Widen(argv[2]), OracleFirstRun(root));
            return 0;
        }

        MigrationTally tally;
        tally.committed = ReadBytes(Widen(F4_COMMITTED_CONFIG));
        tally.defaults = MakeFolder(root, L"global") + L"\\Defaults.ini";
        tally.skewed_defaults = MakeFolder(root, L"skewed-global") + L"\\Defaults.ini";
        WriteBytes(tally.skewed_defaults, kSkewedDefaults);
        {
            const Folders f = NextFolders(root);
            const Migration created =
                Migrate("skewed Defaults.ini", f.migration, std::nullopt, false, tally.skewed_defaults);
            if (created.loaded.status != cfg::ConfigLoadStatus::Created) {
                Fail("skewed Defaults.ini", "no file is not Created");
            }
            tally.skewed = FromMigration(created.loaded.config);
            const Startup builtin = FromMigration(Fallout4HT::MakeConfigTable().defaults());
            for (const Concept row : AllRows()) {
                if (StartupDifferences(builtin, OverDefaults(builtin, {row}, tally.skewed)).empty()) {
                    Fail("skewed Defaults.ini", std::string(cfg::schema::kConcepts[static_cast<size_t>(row)].name) +
                                                    " is the built-in value");
                }
            }
            RemoveFolders(f);
        }

        TestFrozenDefaults();
        TestRegistrationModel();

        const std::string shipped = ReadInput("shipped-dev.ini");
        const std::string firstRun = ReadInput("first-run-dev.ini");
        if (OracleFirstRun(root) != firstRun) {
            Fail("first run", "the oracle's first-run output is not inputs/first-run-dev.ini");
        }

        const std::vector<std::pair<std::string, std::optional<std::string>>> inputs = {
            {"no file", std::nullopt},
            {"empty file", std::string()},
            {"shipped, dev", shipped},
            {"committed, 683fc72", ReadInput("committed-683fc72.ini")},
            {"committed, 4d6c03a", ReadInput("committed-4d6c03a.ini")},
            {"first run, dev", firstRun},
        };
        for (const auto& [name, bytes] : inputs) RunInput(root, name, bytes, tally);
        TestUnopenableFile(root, shipped, tally);

        // Fresh equals upgrade: the file the published build shipped, each version of it
        // committed up to that build and the one it wrote at first launch all convert to the
        // committed file, as no file is created as it.
        for (const auto& [name, bytes] : inputs) {
            if (!bytes || bytes->empty()) continue;
            const Folders f = NextFolders(root);
            const Migration m = Migrate(name, f.migration, bytes, false, tally.defaults);
            if (m.bytes != tally.committed) {
                Fail("fresh equals upgrade", name + " does not convert to the committed file");
            }
            RemoveFolders(f);
        }

        const std::vector<IniMutation> corpus = GenerateIniMutations(shipped, legacy::ReadKeys(), CorpusKeys());
        for (const IniMutation& m : corpus) RunInput(root, "corpus: " + m.name, m.bytes, tally);

        const auto codes = EveryHotkeyCode(shipped);
        for (const auto& [name, bytes] : codes) RunInput(root, name, bytes, tally);

        std::printf("%zu inputs, %zu of them from the corpus and %zu with every hotkey on one code\n",
                    inputs.size() + corpus.size() + codes.size(), corpus.size(), codes.size());
        std::printf("comparison 1, the published build against the frozen reader: %zu listed differences\n",
                    kComparisonOneDifferences.size());
        for (const ListedDifference& d : kComparisonOneDifferences) {
            std::printf("  %s (%s): %d inputs\n    %s\n", d.id, d.commit, d.seen, d.what);
            if (d.seen == 0) Fail(d.id, "a listed difference no input shows");
        }
        std::printf("comparison 2, the frozen reader against the migration: %d created, %d converted, "
                    "%zu distinct files\n",
                    tally.created, tally.converted, tally.migrated.size());
        std::printf("  %d with a changed sensitivity or inversion dropped (pose_shaping)\n",
                    tally.with_pose_shaping_dropped);
        std::printf("  %d with a hotkey code outside 0x01-0xFE unbound (N1)\n", tally.with_n1);
        std::printf("  %d with a hotkey on a Ctrl, Shift or Alt key alone unbound (N3)\n", tally.with_n3);
        if (tally.with_n3 == 0) Fail("N3", "no input unbinds a modifier key");
        std::printf("  %d with a row changed from the frozen default, %d of them the tracking mode\n",
                    tally.with_row_changed, tally.with_mode_changed);
        if (tally.with_row_changed == 0 || tally.with_mode_changed == 0) {
            Fail("Defaults.ini", "no input changes a row, the tracking mode among them, from the frozen default");
        }
        if (tally.with_pose_shaping_dropped == 0) Fail("pose shaping", "no input drops a changed value");
        if (tally.with_n1 == 0) Fail("N1", "no input unbinds an out-of-range code");
        if (tally.migrated.count(tally.committed) == 0) Fail("first run", "no input migrated to the committed file");

        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring lintDir(exe);
        lintDir = lintDir.substr(0, lintDir.find_last_of(L'\\'));
        lintDir = MakeFolder(lintDir, L"migrated");
        int n = 0;
        for (const std::string& file : tally.migrated) {
            WriteBytes(lintDir + L"\\" + std::to_wstring(n++) + L".ini", file);
        }

        for (const wchar_t* name : {L"\\global", L"\\skewed-global"}) {
            const std::wstring global = root + name;
            EmptyFolder(global);
            RemoveDirectoryW(global.c_str());
        }
        RemoveDirectoryW(root.c_str());
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }

    if (g_failures == 0) {
        std::printf("config differential: all passed\n");
        return 0;
    }
    std::printf("config differential: %d failure(s)\n", g_failures);
    return 1;
}
