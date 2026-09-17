// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

namespace gc_ht {

// Why the frame's head pose is or is not applied.
//
// A pure function of two booleans, so the whole walk can be exercised without a
// game. There is no aim-down-sights state here: this build has no source for
// the engine's aim state, and a mode that could never be entered would be a
// setting that lies rather than a feature.
enum class GateReason {
    // Nothing suppresses tracking.
    Gameplay,
    // The main menu, a level load, the pause menu, an inventory screen, or an
    // alt-tab.
    NotGameplay,
    // The player switched tracking off.
    Disabled,
};

// There is deliberately no "no build profile" input. An unrecognised build
// returns from Initialize before the detour is armed, and the game state is
// built before Arm() publishes the hook, so no frame can reach this gate without
// both. A branch for it would be a fallback for a case that cannot happen, and
// its text could never be printed.
struct GateInputs {
    bool gameplay = false;
    bool enabled = false;
};

inline GateReason EvaluateGate(const GateInputs& in) {
    if (!in.gameplay) return GateReason::NotGameplay;
    if (!in.enabled) return GateReason::Disabled;
    return GateReason::Gameplay;
}

// Whether the head pose still reaches the camera under this reason.
inline bool PoseApplies(GateReason reason) { return reason == GateReason::Gameplay; }

inline const char* GateReasonText(GateReason reason) {
    switch (reason) {
        case GateReason::Gameplay:    return "gameplay";
        case GateReason::NotGameplay: return "not in gameplay";
        case GateReason::Disabled:    return "tracking switched off";
    }
    return "?";
}

}  // namespace gc_ht
