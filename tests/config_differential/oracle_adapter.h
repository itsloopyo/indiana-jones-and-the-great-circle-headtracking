#pragma once

// The oracle: the config reader and hotkey registration of the newest published build (the
// rolling `dev` pre-release, d4bf69d), compiled from oracle/ with the core sources they
// included at its pin (c480d8a). Two libraries build it, each with its namespaces renamed at
// compile time so it links beside the current core and the current mod: the reader with the
// published IniReader and value guards, and Hotkeys::Start against oracle_fake's recording
// poller, keyboard and mod. This header names no core or mod type, so the test includes it
// without the renaming.

#include <array>
#include <string>
#include <vector>

namespace gc_oracle_view {

struct OracleConfig {
    int udp_port;
    bool enable_on_startup;
    int toggle_key, cycle_mode_key, yaw_mode_key;
    int chord_toggle_key, chord_cycle_mode_key, chord_yaw_mode_key;
    bool world_space_yaw;
    float local_smoothing, remote_smoothing;
    bool compensate_world_markers;
    bool position_enabled;
    float limit_x, limit_y, limit_z, limit_z_back;
};

// What the published build's LoadSettings ran on a default Config: WriteDefaultConfigIfMissing,
// then LoadConfig, in `dir`. Writes the default file when there is none, as that build did.
OracleConfig RunOracle(const std::string& dir);

// The bindings Hotkeys::Start registers: a nav-cluster code and a chord letter per action.
struct OracleKeys {
    int toggle, cycle_mode, yaw_mode;
    int chord_toggle, chord_cycle_mode, chord_yaw_mode;
};

// Which actions a key press fires, for every key a binding can name (0x01-0xFE) under every
// set of held modifiers. Entry (vk - kFirstKey) * kHeldStates + held counts the toggle, cycle
// mode and yaw mode actions fired, in that order. held: 1 Ctrl, 2 Shift, 4 Alt.
constexpr int kFirstKey = 0x01;
constexpr int kLastKey = 0xFE;
constexpr int kHeldStates = 8;
constexpr int kActions = 3;
using FireTable = std::vector<std::array<int, kActions>>;

// The published build's Hotkeys::Start run with `keys`, pressing each key under each held set.
FireTable OracleFires(const OracleKeys& keys);

}  // namespace gc_oracle_view
