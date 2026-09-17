// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include "builds/build_profile.h"

namespace gc_ht {

class HeadTrackingMod;

// Keeps the parts of the interface that are anchored in the world agreeing with
// the view the frame is actually drawn from.
//
// Two things stop agreeing the moment the camera hook turns the view under the
// game. A world marker - an interaction ring, an objective pin - is projected by
// the engine through its own CLEAN view, so it lands on the pixels the eye was
// looking at before the head moved and slides off the thing it is pinned to. And
// the reticle is drawn at a fixed place on the canvas, which marks where shots
// land only while the rendered view IS the aim.
//
// Both are corrected by hooking the engine's own projection and its own draw:
// the marker projection is handed a copy of the view carrying the head pose, and
// the reticle element is moved to where the clean aim lands in the tracked
// picture. Nothing the game did not already place is drawn, and the game's own
// view object is never written to.
//
// One instance, owned by CameraHook. Started once and never stopped - the
// detours behind it are process-globals that outlive any teardown this mod
// would attempt, which is the same bargain the camera hook itself makes.
class UiWorldView {
public:
    // Installs the interface detours, in the order the profile lists them.
    // False means none of them are live and the interface is drawn exactly as
    // the game draws it; the camera hook then carries on with the marker
    // compensation switched off rather than half installed.
    bool Start(const builds::BuildProfile& profile);

    // Publishes this frame's pose for the detours to read whenever the engine
    // gets round to drawing the elements they sit on - which is not necessarily
    // on this thread, and not necessarily within this frame.
    //
    // `active` false publishes no pose, so the interface is drawn against the
    // game's own view. Called once per frame from the render hook, whether or
    // not a pose was written, so a suppressed frame is published as suppressed
    // rather than leaving the previous frame's pose standing.
    void SetFrame(const HeadTrackingMod& mod, bool active);
};

}  // namespace gc_ht
