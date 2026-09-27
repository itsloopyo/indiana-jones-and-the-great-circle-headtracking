// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
//
// The default HeadTracking.ini the mod writes on a first launch. The reader's
// boundary cases are in tests/config_differential/legacy_reader_tests.cpp.
//
// The first test is the config_defaults check src/config.cpp names: it writes
// the shipped default INI, loads it back over a deliberately poisoned Config,
// and requires every field to land on the built-in default. That is what keeps
// the default file text and the Config member initialisers from drifting apart,
// which is otherwise invisible until a player reports a setting doing nothing.
//
// Each case gets its OWN directory. GetPrivateProfileString caches by path, and
// rewriting one file under one name several times in a run is the shape that
// reads back a previous case's values.

#include <windows.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "check_harness.h"
#include "config.h"
#include "legacy_config/legacy_config.h"
#include "logging.h"

namespace {

using checks::Check;
using checks::CheckNear;

using gc_ht::Config;

const wchar_t* const kLogPathWide = L"config_tests.log";
const char* const kLogPath = "config_tests.log";

constexpr char kIniName[] = "HeadTracking.ini";

std::string g_root;
std::vector<std::string> g_caseDirs;

std::string ReadWholeFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

// %TEMP%\gc_ht_config_tests_<pid>. Absolute on purpose: a relative path
// handed to GetPrivateProfileString is resolved against the Windows directory,
// not the working directory, so a case built on one would read nothing.
bool MakeRoot() {
    char temp[MAX_PATH] = {};
    const DWORD length = GetTempPathA(static_cast<DWORD>(sizeof(temp)), temp);
    if (length == 0 || length >= sizeof(temp)) return false;

    char dir[MAX_PATH] = {};
    std::snprintf(dir, sizeof(dir), "%sgc_ht_config_tests_%lu", temp, GetCurrentProcessId());
    if (!CreateDirectoryA(dir, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    g_root = dir;
    return true;
}

std::string CaseDir(const char* name) {
    const std::string dir = g_root + "\\" + name;
    CreateDirectoryA(dir.c_str(), nullptr);
    // A leftover from an interrupted run would defeat WriteDefaultConfigIfMissing's
    // CREATE_NEW and make the defaults case read a file this run never wrote.
    DeleteFileA((dir + "\\" + kIniName).c_str());
    g_caseDirs.push_back(dir);
    return dir;
}

void WriteIni(const std::string& dir, const std::string& body) {
    const std::string path = dir + "\\" + kIniName;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    Check(file.is_open(), "the test can write an INI to the case directory");
    file << body;
}

// Every field set to something no default is, so a field the loader never
// touches shows up as itself rather than passing by accident.
gc_ht::legacy::Config Poisoned() {
    gc_ht::legacy::Config c;
    c.udp_port = 1234;
    c.enable_on_startup = false;
    c.toggle_key = 0x41;
    c.cycle_mode_key = 0x42;
    c.yaw_mode_key = 0x43;
    c.chord_toggle_key = 0x45;
    c.chord_cycle_mode_key = 0x46;
    c.chord_yaw_mode_key = 0x49;
    c.world_space_yaw = false;
    c.compensate_world_markers = false;
    c.local_smoothing = 0.77f;
    c.remote_smoothing = 0.88f;
    c.position_enabled = false;
    c.limit_x = 0.11f;
    c.limit_y = 0.12f;
    c.limit_z = 0.13f;
    c.limit_z_back = 0.14f;
    return c;
}

// ---- The shipped default file has to reproduce the built-in defaults ---------

void TestShippedDefaultIniReproducesTheBuiltInDefaults() {
    const std::string dir = CaseDir("defaults");
    gc_ht::WriteDefaultConfigIfMissing(dir);
    Check(!ReadWholeFile(dir + "\\" + kIniName).empty(),
          "WriteDefaultConfigIfMissing writes a default HeadTracking.ini");

    gc_ht::legacy::Config loaded = Poisoned();
    Check(gc_ht::legacy::Read(dir + "\\" + kIniName, loaded) == gc_ht::legacy::ReadStatus::Read,
          "the default HeadTracking.ini is read");

    const Config d;
    Check(loaded.udp_port == d.udp_port, "default INI: UdpPort is the built-in default");
    Check(loaded.enable_on_startup == d.enable_on_startup,
          "default INI: EnableOnStartup is the built-in default");
    Check(loaded.world_space_yaw == d.world_space_yaw,
          "default INI: WorldSpaceYaw is the built-in default");
    Check(loaded.compensate_world_markers,
          "default INI: marker and reticle compensation is enabled");
    Check(loaded.toggle_key == d.toggle_key, "default INI: ToggleKey is the built-in default");
    Check(loaded.cycle_mode_key == d.cycle_mode_key,
          "default INI: CycleModeKey is the built-in default");
    Check(loaded.yaw_mode_key == d.yaw_mode_key, "default INI: YawModeKey is the built-in default");
    Check(loaded.chord_toggle_key == d.chord_toggle_key,
          "default INI: ChordToggleKey is the built-in default");
    Check(loaded.chord_cycle_mode_key == d.chord_cycle_mode_key,
          "default INI: ChordCycleModeKey is the built-in default");
    Check(loaded.chord_yaw_mode_key == d.chord_yaw_mode_key,
          "default INI: ChordYawModeKey is the built-in default");
    CheckNear(loaded.local_smoothing, d.local_smoothing, 1e-6f,
              "default INI: LocalSmoothing is the built-in default");
    CheckNear(loaded.remote_smoothing, d.remote_smoothing, 1e-6f,
              "default INI: RemoteSmoothing is the built-in default");
    Check(loaded.position_enabled == d.position_enabled,
          "default INI: Position Enabled is the built-in default");
    CheckNear(loaded.limit_x, d.limit_x, 1e-6f, "default INI: LimitX is the built-in default");
    CheckNear(loaded.limit_y, d.limit_y, 1e-6f, "default INI: LimitY is the built-in default");
    CheckNear(loaded.limit_z, d.limit_z, 1e-6f, "default INI: LimitZ is the built-in default");
    CheckNear(loaded.limit_z_back, d.limit_z_back, 1e-6f,
              "default INI: LimitZBack is the built-in default");
}

// Every hotkey check in this file compared the loaded value against a
// default-constructed Config, so the codes themselves were free: a commit that
// changed config.h and the shipped INI text together passed. These are the
// literal codes the shared controls table names, and the two keys it says must
// stay unbound.
void TestTheDefaultBindingsAreTheOnesTheControlsTableNames() {
    const Config d;
    Check(d.toggle_key == 0x23, "toggle is End (0x23)");
    Check(d.cycle_mode_key == 0x21, "cycle tracking mode is Page Up (0x21)");
    Check(d.yaw_mode_key == 0x22, "toggle yaw mode is Page Down (0x22)");

    // The chord cluster, in the action order the table fixes so the same action
    // lands on the same chord in every mod.
    Check(d.chord_toggle_key == 0x59, "toggle chord is Ctrl+Shift+Y");
    Check(d.chord_cycle_mode_key == 0x47, "cycle-mode chord is Ctrl+Shift+G");
    Check(d.chord_yaw_mode_key == 0x48, "yaw-mode chord is Ctrl+Shift+H");

    // Home and Ctrl+Shift+T were the recenter pair before mods stopped keeping a
    // centre. Binding either to something else fires on muscle memory, so they
    // stay free.
    const int nav[] = {d.toggle_key, d.cycle_mode_key, d.yaw_mode_key};
    const int chord[] = {d.chord_toggle_key, d.chord_cycle_mode_key, d.chord_yaw_mode_key,};
    bool homeFree = true, chordTFree = true;
    for (int key : nav) {
        if (key == 0x24) homeFree = false;
    }
    for (int key : chord) {
        if (key == 0x54) chordTFree = false;
    }
    Check(homeFree, "Home (0x24) is left unbound - it was the recenter key");
    Check(chordTFree, "Ctrl+Shift+T (0x54) is left unbound - it was the recenter chord");
}

void TestAnExistingConfigIsNeverOverwritten() {
    const std::string dir = CaseDir("preserve");
    const std::string mine = "[Network]\nUdpPort=5555\n";
    WriteIni(dir, mine);

    gc_ht::WriteDefaultConfigIfMissing(dir);
    Check(ReadWholeFile(dir + "\\" + kIniName) == mine,
          "an existing HeadTracking.ini is left byte for byte as the player wrote it");
}

void Cleanup() {
    for (const std::string& dir : g_caseDirs) {
        DeleteFileA((dir + "\\" + kIniName).c_str());
        RemoveDirectoryA(dir.c_str());
    }
    if (!g_root.empty()) RemoveDirectoryA(g_root.c_str());
    std::remove(kLogPath);
    std::remove("config_tests.prev.log");
}

}  // namespace

int main() {
    if (!MakeRoot()) {
        Check(false, "the test can create a temporary directory to hold its INI files");
        return checks::Summarize("config");
    }

    cameraunlock::logging::Open(kLogPathWide);

    TestShippedDefaultIniReproducesTheBuiltInDefaults();
    TestTheDefaultBindingsAreTheOnesTheControlsTableNames();
    TestAnExistingConfigIsNeverOverwritten();
    cameraunlock::logging::Close();

    Cleanup();
    return checks::Summarize("config");
}
