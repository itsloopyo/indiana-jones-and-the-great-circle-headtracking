// The config differential test (convert-a-mod-to-the-canonical-config, section 5).
//
// Oracle: the reader of the newest published build, the rolling `dev` pre-release at d4bf69d,
// with the core sources it compiled at its pin c480d8a, and its Hotkeys::Start
// (oracle_adapter.h).
// Import: the frozen reader in src/legacy_config/.
//
// Comparison 1, oracle against import, on every input: load status, every field both read
// (floats bit for bit), which amounts to the startup state (tracking on or off, the tracking
// mode, the yaw mode), and which actions every key press fires under every set of held
// modifiers. Nothing that reads the file changed between d4bf69d and the frozen reader, so it
// may find no difference.
//
// Also asserted after every import: the folder, HeadTracking.ini's bytes, last write time and
// attributes included, is as the import found it, and a read-only copy imports as a writable
// one does.
//
// Inputs: no file, an empty file, the file the dev build writes at first launch when there is
// none (its installer ZIP carried no config and its launcher manifest seeds nothing, and the
// repo never tracked one, so that is the only file it put on a player's disk; extracted once
// into data/ with --first-run), core's corpus over it, and that file with all six hotkey rows
// on each code from 0x01 to 0xFE.
//
// `--first-run <path>` writes the oracle's first-run output to <path> and exits.

#include "legacy_config/legacy_config.h"
#include "oracle_adapter.h"

#include "cameraunlock/config/testing/ini_mutations.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
namespace legacy = gc_ht::legacy;

namespace {

constexpr const char* kFileName = "HeadTracking.ini";

int g_failures = 0;
int g_checks = 0;

void Check(bool cond, const std::string& what) {
    ++g_checks;
    if (!cond) {
        if (g_failures < 200) std::printf("  FAIL: %s\n", what.c_str());
        ++g_failures;
    }
}

bool SameBits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("could not read " + path.string());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("could not write " + path.string());
}

// Every file in a folder with its bytes, its last write time and its attributes.
struct Entry {
    std::string name;
    std::string bytes;
    FILETIME written;
    DWORD attributes;
    bool operator==(const Entry& o) const {
        return name == o.name && bytes == o.bytes && CompareFileTime(&written, &o.written) == 0 &&
               attributes == o.attributes;
    }
    bool operator<(const Entry& o) const { return name < o.name; }
};

std::vector<Entry> List(const fs::path& dir) {
    std::vector<Entry> entries;
    for (const auto& e : fs::directory_iterator(dir)) {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExW(e.path().c_str(), GetFileExInfoStandard, &data)) {
            throw std::runtime_error("no attributes for " + e.path().string());
        }
        entries.push_back({e.path().filename().string(), ReadBytes(e.path()), data.ftLastWriteTime,
                           data.dwFileAttributes});
    }
    std::sort(entries.begin(), entries.end());
    return entries;
}

void SetReadOnly(const fs::path& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) throw std::runtime_error("no attributes for " + path.string());
    if (!SetFileAttributesW(path.c_str(), attrs | FILE_ATTRIBUTE_READONLY)) {
        throw std::runtime_error("could not set attributes on " + path.string());
    }
}

struct Input {
    std::string name;
    std::optional<std::string> bytes;  // nullopt: no file
};

std::vector<std::string> FieldDifferences(const gc_oracle_view::OracleConfig& o, const legacy::Config& i) {
    std::vector<std::string> d;
    auto b = [&d](const char* n, bool x, bool y) { if (x != y) d.push_back(n); };
    auto f = [&d](const char* n, float x, float y) { if (!SameBits(x, y)) d.push_back(n); };
    auto n = [&d](const char* name, int x, int y) { if (x != y) d.push_back(name); };
    n("udp_port", o.udp_port, i.udp_port);
    b("enable_on_startup", o.enable_on_startup, i.enable_on_startup);
    n("toggle_key", o.toggle_key, i.toggle_key);
    n("cycle_mode_key", o.cycle_mode_key, i.cycle_mode_key);
    n("yaw_mode_key", o.yaw_mode_key, i.yaw_mode_key);
    n("chord_toggle_key", o.chord_toggle_key, i.chord_toggle_key);
    n("chord_cycle_mode_key", o.chord_cycle_mode_key, i.chord_cycle_mode_key);
    n("chord_yaw_mode_key", o.chord_yaw_mode_key, i.chord_yaw_mode_key);
    b("world_space_yaw", o.world_space_yaw, i.world_space_yaw);
    f("local_smoothing", o.local_smoothing, i.local_smoothing);
    f("remote_smoothing", o.remote_smoothing, i.remote_smoothing);
    b("compensate_world_markers", o.compensate_world_markers, i.compensate_world_markers);
    b("position_enabled", o.position_enabled, i.position_enabled);
    f("limit_x", o.limit_x, i.limit_x);
    f("limit_y", o.limit_y, i.limit_y);
    f("limit_z", o.limit_z, i.limit_z);
    f("limit_z_back", o.limit_z_back, i.limit_z_back);
    return d;
}

std::string Join(const std::vector<std::string>& v) {
    std::string s;
    for (const std::string& x : v) s += (s.empty() ? "" : ", ") + x;
    return s;
}

gc_oracle_view::OracleKeys ImportKeys(const legacy::Config& c) {
    return {c.toggle_key, c.cycle_mode_key, c.yaw_mode_key,
            c.chord_toggle_key, c.chord_cycle_mode_key, c.chord_yaw_mode_key};
}

gc_oracle_view::OracleKeys OracleKeysOf(const gc_oracle_view::OracleConfig& o) {
    return {o.toggle_key, o.cycle_mode_key, o.yaw_mode_key,
            o.chord_toggle_key, o.chord_cycle_mode_key, o.chord_yaw_mode_key};
}

// The first press whose fired actions differ, for the failure message; "none" when they agree.
std::string FireDifference(const gc_oracle_view::FireTable& expected, const gc_oracle_view::FireTable& got) {
    if (expected.size() != got.size()) return "the tables differ in size";
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (expected[i] == got[i]) continue;
        char text[160];
        std::snprintf(text, sizeof text, "key 0x%02X held %d fires %d/%d/%d, not %d/%d/%d",
                      static_cast<int>(i / gc_oracle_view::kHeldStates) + gc_oracle_view::kFirstKey,
                      static_cast<int>(i % gc_oracle_view::kHeldStates), got[i][0], got[i][1], got[i][2],
                      expected[i][0], expected[i][1], expected[i][2]);
        return text;
    }
    return "none";
}

// The corpus descriptor of every key the frozen reader reads.
std::vector<cameraunlock::config::testing::MutationKey> MutationKeys() {
    using cameraunlock::config::testing::MutationKey;
    auto plain = [](const char* s, const char* k, const char* alt, std::vector<std::string> oor = {}) {
        MutationKey m;
        m.section = s;
        m.key = k;
        m.alternate = alt;
        m.out_of_range = std::move(oor);
        return m;
    };
    // The reader refuses a code outside 0x01-0xFE and every Ctrl, Shift and Alt code (0x10-0x12,
    // 0xA0-0xA5).
    auto hotkey = [&plain](const char* k, const char* alt) {
        MutationKey m = plain("Hotkeys", k, alt, {"0x0", "0x10", "0xA0", "0xFF"});
        m.hotkey = true;
        return m;
    };
    return {
        plain("Network", "UdpPort", "4243", {"1023", "65536"}),
        plain("General", "EnableOnStartup", "0"),
        plain("General", "WorldSpaceYaw", "0"),
        plain("General", "CompensateWorldMarkers", "0"),
        hotkey("ToggleKey", "0x70"),
        hotkey("CycleModeKey", "0x71"),
        hotkey("YawModeKey", "0x72"),
        hotkey("ChordToggleKey", "0x4A"),
        hotkey("ChordCycleModeKey", "0x4B"),
        hotkey("ChordYawModeKey", "0x4C"),
        plain("Rotation", "LocalSmoothing", "0.3", {"-0.5", "1.5"}),
        plain("Rotation", "RemoteSmoothing", "0.3", {"-0.5", "1.5"}),
        plain("Rotation", "Smoothing", "0.5"),
        plain("Position", "Smoothing", "0.5"),
        plain("Position", "Enabled", "0"),
        plain("Position", "LimitX", "0.25", {"-0.1", "0.6"}),
        plain("Position", "LimitY", "0.25", {"-0.1", "0.6"}),
        plain("Position", "LimitZ", "0.25", {"-0.1", "0.6"}),
        plain("Position", "LimitZBack", "0.25", {"-0.1", "0.6"}),
    };
}

class Scratch {
public:
    Scratch() {
        root_ = fs::temp_directory_path() / ("gc-config-differential-" + std::to_string(GetCurrentProcessId()));
        fs::remove_all(root_);
        fs::create_directories(root_);
    }
    ~Scratch() {
        std::error_code ec;
        for (const auto& e : fs::recursive_directory_iterator(root_, ec)) {
            if (e.is_regular_file()) SetFileAttributesW(e.path().c_str(), FILE_ATTRIBUTE_NORMAL);
        }
        fs::remove_all(root_, ec);
    }
    // Every folder, once an input is done with them, so the run holds a few folders on disk at a
    // time rather than thousands. Each input still gets folders of its own: GetPrivateProfile*
    // caches by path, and one file rewritten under one name reads back another input's values.
    void Clear() {
        for (const auto& dir : fs::directory_iterator(root_)) {
            for (const auto& e : fs::recursive_directory_iterator(dir.path())) {
                if (e.is_regular_file()) SetFileAttributesW(e.path().c_str(), FILE_ATTRIBUTE_NORMAL);
            }
            fs::remove_all(dir.path());
        }
    }
    fs::path Fresh(const std::string& leaf) {
        const fs::path dir = root_ / (leaf + std::to_string(next_++));
        fs::create_directories(dir);
        return dir;
    }

private:
    fs::path root_;
    int next_ = 0;
};

fs::path Place(const fs::path& dir, const Input& input) {
    const fs::path file = dir / kFileName;
    if (input.bytes) WriteBytes(file, *input.bytes);
    return file;
}

struct ImportRun {
    legacy::Config config;
    legacy::ReadStatus status = legacy::ReadStatus::Read;
};

gc_oracle_view::OracleConfig View(const legacy::Config& c) {
    gc_oracle_view::OracleConfig o{};
    o.udp_port = c.udp_port;
    o.enable_on_startup = c.enable_on_startup;
    o.toggle_key = c.toggle_key;
    o.cycle_mode_key = c.cycle_mode_key;
    o.yaw_mode_key = c.yaw_mode_key;
    o.chord_toggle_key = c.chord_toggle_key;
    o.chord_cycle_mode_key = c.chord_cycle_mode_key;
    o.chord_yaw_mode_key = c.chord_yaw_mode_key;
    o.world_space_yaw = c.world_space_yaw;
    o.local_smoothing = c.local_smoothing;
    o.remote_smoothing = c.remote_smoothing;
    o.compensate_world_markers = c.compensate_world_markers;
    o.position_enabled = c.position_enabled;
    o.limit_x = c.limit_x;
    o.limit_y = c.limit_y;
    o.limit_z = c.limit_z;
    o.limit_z_back = c.limit_z_back;
    return o;
}

bool SameImport(const ImportRun& a, const ImportRun& b) {
    return a.status == b.status && FieldDifferences(View(a.config), b.config).empty();
}

// The import on one copy of the input, which it must leave as it found it.
ImportRun RunImport(Scratch& scratch, const Input& input, bool readOnly) {
    const fs::path dir = scratch.Fresh(readOnly ? "import-ro" : "import");
    const fs::path file = Place(dir, input);
    if (input.bytes && readOnly) SetReadOnly(file);
    const std::vector<Entry> before = List(dir);
    ImportRun run;
    run.status = legacy::Read(file.string(), run.config);
    Check(List(dir) == before, input.name + ": the import changed its folder");
    return run;
}

ImportRun Comparison1(Scratch& scratch, const Input& input) {
    const fs::path odir = scratch.Fresh("oracle");
    Place(odir, input);
    const gc_oracle_view::OracleConfig oracle = gc_oracle_view::RunOracle(odir.string());
    const ImportRun import = RunImport(scratch, input, false);
    const ImportRun readOnly = RunImport(scratch, input, true);
    Check(SameImport(import, readOnly), input.name + ": a read-only copy imports differently");

    // The published build never refused a file: a missing one it wrote and then read.
    Check((import.status == legacy::ReadStatus::Absent) == !input.bytes.has_value(),
          input.name + ": the import's status");
    const std::vector<std::string> fields = FieldDifferences(oracle, import.config);
    Check(fields.empty(), input.name + ": fields differ: " + Join(fields));

    const gc_oracle_view::FireTable oracleFires = gc_oracle_view::OracleFires(OracleKeysOf(oracle));
    const gc_oracle_view::FireTable importFires = gc_oracle_view::OracleFires(ImportKeys(import.config));
    Check(FireDifference(oracleFires, importFires) == "none",
          input.name + ": hotkeys fire differently: " + FireDifference(oracleFires, importFires));
    return import;
}

// The first-run file with one line's value replaced; the line must be there.
std::string WithValue(const std::string& firstRun, const std::string& key, const std::string& value) {
    const std::string marker = "\n" + key + "=";
    const std::size_t at = firstRun.find(marker);
    if (at == std::string::npos) throw std::logic_error(key + " has no line in the first-run file");
    const std::size_t start = at + marker.size();
    const std::size_t end = firstRun.find('\n', start);
    return firstRun.substr(0, start) + value + firstRun.substr(end);
}

std::vector<Input> Inputs(const std::string& firstRun) {
    using cameraunlock::config::testing::GenerateIniMutations;
    std::vector<Input> inputs;
    inputs.push_back({"no file", std::nullopt});
    inputs.push_back({"empty file", std::string()});
    inputs.push_back({"dev first-run output", firstRun});
    for (auto& m : GenerateIniMutations(firstRun, legacy::ReadKeys(), MutationKeys())) {
        inputs.push_back({"corpus: " + m.name, std::move(m.bytes)});
    }
    const char* hotkeys[] = {"ToggleKey", "CycleModeKey", "YawModeKey",
                             "ChordToggleKey", "ChordCycleModeKey", "ChordYawModeKey"};
    for (int vk = 0x01; vk <= 0xFE; ++vk) {
        char code[8];
        std::snprintf(code, sizeof code, "0x%02X", vk);
        std::string bytes = firstRun;
        for (const char* key : hotkeys) bytes = WithValue(bytes, key, code);
        inputs.push_back({std::string("every hotkey row ") + code, std::move(bytes)});
    }
    return inputs;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--first-run") == 0) {
        Scratch scratch;
        const fs::path dir = scratch.Fresh("first-run");
        gc_oracle_view::RunOracle(dir.string());
        WriteBytes(argv[2], ReadBytes(dir / kFileName));
        std::printf("wrote %s\n", argv[2]);
        return 0;
    }
    try {
        Scratch scratch;

        // The dev build's first-run output, committed once as test data, is what the oracle
        // still writes for a missing file.
        const std::string firstRun = ReadBytes(fs::path(GC_DIFFERENTIAL_DATA) / "dev-first-run.ini");
        {
            const fs::path dir = scratch.Fresh("first-run");
            gc_oracle_view::RunOracle(dir.string());
            Check(ReadBytes(dir / kFileName) == firstRun,
                  "the oracle's first-run output differs from data/dev-first-run.ini");
        }

        const std::vector<Input> inputs = Inputs(firstRun);
        std::printf("comparison 1 (oracle dev d4bf69d against the import) on %zu inputs\n", inputs.size());
        for (const Input& input : inputs) {
            Comparison1(scratch, input);
            scratch.Clear();
        }
    } catch (const std::exception& e) {
        std::printf("  FAIL: threw: %s\n", e.what());
        ++g_failures;
    }

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
