// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include <cstdint>

namespace gc_ht {

// Rewrites the position in the game's own window placements so they land centred
// on the work area of the monitor it is placing the window on. The game puts its
// window up at the position saved in TheGreatCircleConfig.local and then places
// it again about fifteen seconds later, while it is still starting up. The second
// placement was measured with the mod parked, so it is the game's own and not a
// reaction to being moved, and it asks for a position the game works out itself
// (3824, 421 for a 1296x759 window on a 5120x1440 monitor). Centring the window
// once before that happens therefore does not survive it, which is what this
// covers.
//
// Only placements made from the game's own module are touched, so a window the
// player drags is left where they put it. A placement as large as the work area
// (fullscreen or borderless) passes through unchanged. A failure to install is
// logged and leaves the window where the game puts it.
void StartWindowCentering(std::uintptr_t imageBase, std::uint32_t imageSize);

// Waits for the game to bring its render window up and hold it still, then
// centres it on the work area of the monitor it is already on. A window that
// fills the screen, or that the game placed centred itself, is left alone - so
// this only ever moves a windowed-mode game, and it moves it once.
//
// This is what centres the window the game created already placed: the position
// CreateWindowEx was given never passes through the placement hook above.
//
// Blocks until the window settles or the wait times out, so call it after
// everything else the startup thread has to do.
void CenterWindowWhenReady();

}  // namespace gc_ht
