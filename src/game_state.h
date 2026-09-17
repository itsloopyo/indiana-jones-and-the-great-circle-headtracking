// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include <cstdint>

#include "idtech/resolver_thread.h"

namespace gc_ht {

// Whether the player is in control of the view right now.
//
// Head tracking is suppressed during a level load and whenever the engine has
// stopped advancing game time, which is the pause menu, an inventory screen and
// an alt-tab alike - so a menu the player is reading does not swim about while
// they look at the keyboard. The MAIN menu is not covered; see below for why.
//
// Both engine objects behind this are ECS components: plain structs with no
// vtable and no global pointing at them, so each is found by sweeping committed
// memory for a block whose own fields match the layout the engine's field
// reflection tables declare for it. The shapes below are what that sweep tests,
// and every offset in them is read straight off those tables rather than
// guessed.
class GameState {
public:
    GameState();

    // Two guarded loads off pointers another thread resolved, so this is cheap
    // enough for the per-frame call the render hook makes.
    bool IsGameplay();

    // The last state that was actually observed, for the log line.
    const char* LastReason() const { return m_reason; }

    // Whether LastReason() is currently one of the two the resolver alternates
    // between while it has nothing published. Those two flip every three seconds
    // for as long as the frame block cannot be read, so a caller that logs
    // transitions has to bound them - every other reason here changes only when
    // the player or the game does something, and bounding those would silence
    // the transitions that matter.
    bool ReasonIsResolverChurn() const;

private:
    // gameFrameParms_t, the per-frame block the engine hands its own frame
    // setup. Found by its first six ints - the render, upsample and scanout
    // sizes - followed by a run of bools, which nothing else in the process
    // looks like.
    idtech::ResolverThread m_frameParms;

    // There is deliberately no main-menu check here.
    //
    // idGameInfoComponent carries an isMainMenu flag, and a content sweep for
    // that component was tried. It is not identifiable: the sweep landed on a
    // different block on each run, and the flag it read was whatever that block
    // happened to hold. On one run it read 0 on the main menu, so tracking ran
    // there; on the next it read 1 during gameplay, so tracking was suppressed
    // for the whole session with the log confidently reporting "main menu".
    //
    // The second of those is much worse than the gap it was meant to close. A
    // camera that drifts on the menu is a nuisance; a mod that silently does
    // nothing is indistinguishable from a broken one, and it is the report that
    // costs an evening. So the check is out until the component can be found by
    // something better than what its bytes look like.

    const char* m_reason = "";
    // What each sweep has already been reported as finding, so the line is
    // written once per object rather than once per frame.
    void* m_reportedParms = nullptr;
};

}  // namespace gc_ht
