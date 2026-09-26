// SPDX-License-Identifier: MIT
//
// The config differential test. Every input is read two ways:
//
//   oracle     the published build's reader (oracle/), the dev pre-release at fbefff4, with
//              its Mod::LoadConfig
//   import     the frozen reader in src/legacy_config/
//
// Comparison 1, oracle against import, finds what a player updating from the published build
// sees change that the conversion did not cause. Every difference it may find is listed in
// kComparisonOneDifferences with the commit that made it; any other fails the test. The frozen
// reader is the published one byte for byte, and every core source both compile is the same
// bytes at the published pin, so the list is empty.
//
// Inputs: no file, an empty file, the HeadTracking.ini the dev pre-release shipped (its
// installer ZIP's plugins\HeadTracking.ini; its launcher manifest seeds nothing), each distinct
// version of that file committed up to it, the file that build writes at first launch when
// there is none (extracted once into inputs/ with --first-run), core's corpus over the shipped
// file, and the shipped file with all three hotkeys on each code from 0x01 to 0xFE.

#include "legacy_config/legacy_config.h"
#include "oracle_adapter.h"

#include "cameraunlock/config/testing/ini_mutations.h"
#include "cameraunlock/input/key_bindings.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

namespace legacy = Fallout4HT::legacy;
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
};

Folders NextFolders(const std::wstring& root) {
    static int n = 0;
    const std::wstring dir = MakeFolder(root, std::to_wstring(n++).c_str());
    return {dir, MakeFolder(dir, L"oracle"), MakeFolder(dir, L"import")};
}

void RemoveFolders(const Folders& f) {
    for (const std::wstring& dir : {f.oracle, f.import, f.root}) {
        EmptyFolder(dir);
        if (!RemoveDirectoryW(dir.c_str())) throw std::runtime_error("cannot remove a test folder");
    }
}

const wchar_t kIniName[] = L"HeadTracking.ini";

void RunInput(const std::wstring& root, const std::string& name, const std::optional<std::string>& bytes) {
    const Folders f = NextFolders(root);
    OracleRun o;
    {
        const std::wstring path = f.oracle + L"\\" + kIniName;
        if (bytes) WriteBytes(path, *bytes);
        o.status = oracle_api::LoadOrCreate(Narrow(path).c_str(), o.cfg);
    }

    ImportRun i;
    {
        const std::wstring path = f.import + L"\\" + kIniName;
        if (bytes) {
            WriteBytes(path, *bytes);
            SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY);
        }
        const auto before = Snapshot(f.import);
        i.status = legacy::Read(Narrow(path).c_str(), i.cfg);
        if (Snapshot(f.import) != before) Fail(name, "the import changed the folder it read from");
        if (!bytes && i.status != legacy::ReadStatus::Absent) Fail(name, "the import read a file that is not there");
    }

    CompareOracleWithImport(name, o, i);
    RemoveFolders(f);
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

        TestFrozenDefaults();

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
        for (const auto& [name, bytes] : inputs) RunInput(root, name, bytes);

        const std::vector<IniMutation> corpus = GenerateIniMutations(shipped, legacy::ReadKeys(), CorpusKeys());
        for (const IniMutation& m : corpus) RunInput(root, "corpus: " + m.name, m.bytes);

        const auto codes = EveryHotkeyCode(shipped);
        for (const auto& [name, bytes] : codes) RunInput(root, name, bytes);

        std::printf("%zu inputs, %zu of them from the corpus and %zu with every hotkey on one code\n",
                    inputs.size() + corpus.size() + codes.size(), corpus.size(), codes.size());
        std::printf("comparison 1, the published build against the frozen reader: %zu listed differences\n",
                    kComparisonOneDifferences.size());
        for (const ListedDifference& d : kComparisonOneDifferences) {
            std::printf("  %s (%s): %d inputs\n    %s\n", d.id, d.commit, d.seen, d.what);
            if (d.seen == 0) Fail(d.id, "a listed difference no input shows");
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
