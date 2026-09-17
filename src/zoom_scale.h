// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include <cmath>

#include "angle_units.h"
#include "cameraunlock/camera/zoom_compensation.h"

namespace gc_ht {

// Keeps head tracking worth the same distance across the screen whatever the
// game is doing with its field of view.
//
// The game narrows the view to raise and aim a weapon, and the player can move
// it themselves with the Field of View slider in the video settings. A narrow
// field magnifies everything in the frame, head tracking included: the head
// still turns ten degrees and the camera still turns ten degrees, and the
// picture simply moves further. Without this the player reads that as the mod's
// sensitivity jumping the moment they aim.
//
// It is a boundary conversion in the same family as the axis signs and the unit
// scale, not a setting. Nothing here is configurable, and the factor is exactly
// 1 whenever the game is drawing its ordinary field of view.
struct ZoomScale {
    // The margin short of the quarter turn at which Angle stops scaling. The
    // round trip turns into a reflection AT 90; this sits just inside it.
    static constexpr float kMaxScalableDegrees = 89.0f;

    // False when either angle came back as something a projection cannot be
    // built from. The pose then goes through untouched rather than scaled by a
    // guess, which is the only honest answer when the field of view cannot be
    // read.
    bool known = false;
    float factor = 1.0f;

    // Yaw and pitch. Both translate the picture across the frame, so both take
    // the factor. ROLL DOES NOT: it turns the picture about the view axis by the
    // same angle at every field of view there is, so scaling it would flatten a
    // head tilt the player is holding and buy nothing.
    //
    // ScaleAngleForZoom is a tangent round trip, and it is only a scaling inside
    // a quarter turn: tan is negative in the second quadrant while atan answers
    // in the first, so an unguarded 100 degrees came back as -80 and a head
    // sweeping through 90 flipped the view half a turn between two frames. It
    // did that at factor 1.0 as well, which is ordinary play, and a tracker
    // profile amplified for shoulder checks reaches there.
    //
    // So the round trip is taken on the angle SATURATED at the quarter turn and
    // the remainder is added back. That keeps the sign, and it is continuous at
    // the join by construction - both branches agree exactly at the limit -
    // where simply passing the far side through unscaled would step by 1.4
    // degrees at a 2.4x zoom. Past the limit there is no screen displacement
    // left to preserve anyway.
    float Angle(float degrees) const {
        if (!known) return degrees;
        const float saturated = degrees > kMaxScalableDegrees    ?  kMaxScalableDegrees
                                : degrees < -kMaxScalableDegrees ? -kMaxScalableDegrees
                                                                 : degrees;
        return cameraunlock::camera::ScaleAngleForZoom(saturated, factor) + (degrees - saturated);
    }

    // A lean. A head offset d seen at depth D lands at d / (2 D tan(fov/2)) of
    // the frame, so the offset scales linearly and exactly.
    float Length(float units) const { return known ? units * factor : units; }
};

// The aspect the engine derives its VERTICAL field of view at, and the reason
// this conversion works in the vertical rather than the horizontal.
//
// renderView_t carries both angles, and only one of them is the game's own
// number. Measured in two windows on the same machine, same g_fov of 90:
//
//   borderless 5120x1440   fov_x 90.0000  fov_y 58.7155   viewport aspect 1.77778
//   windowed   1920x600    fov_x 90.0179  fov_y 58.7155   viewport aspect 1.77833
//
// fov_y does not move, and 58.71551 is exactly 2*atan(tan(g_fov/2) * 9/16). The
// game renders a 16:9 viewport whatever shape the window is - it pillarboxes on
// this machine's 32:9 display - and widens fov_x to whatever that viewport
// rounds to in whole pixels: 1067/600 is 1.77833 to five figures, which is where
// the second row's extra 0.018 of a degree comes from.
//
// So fov_x is the DERIVED one and pairing it with g_fov is the trap. It happens
// to be within 0.03% here only because the game refuses to render anything but
// 16:9; on an engine build that honoured a 32:9 viewport the same pairing would
// read 2.0 through all of ordinary play and double every head movement, with no
// symptom beyond the tracking feeling twice as strong as the tracker's own
// profile says it is.
inline constexpr float kEngineReferenceAspect = 16.0f / 9.0f;

// What the mod will accept as a field of view out of game memory. This is the
// boundary check: both numbers are read from the running process, one from a
// struct the renderer fills and one from a cvar in .data.
inline bool IsUsableFov(float degrees) {
    return std::isfinite(degrees) && degrees > 0.0f && degrees < 180.0f;
}

// `drawnFovYDegrees` is renderView_t::fov_y, the full VERTICAL angle the frame
// was drawn at. `sliderFovXDegrees` is g_fov, the full HORIZONTAL angle the
// player's Field of View slider holds. The two are converted into the same axis
// here, once, which is the whole job: a base and a live value in different axes
// is a silent constant on every pose in ordinary play.
inline ZoomScale ComputeZoomScale(float drawnFovYDegrees, float sliderFovXDegrees) {
    ZoomScale scale;
    if (!IsUsableFov(drawnFovYDegrees) || !IsUsableFov(sliderFovXDegrees)) return scale;

    const float tanBaseY =
        std::tan(sliderFovXDegrees * kHalfDegreesToRadians) / kEngineReferenceAspect;
    const float factor = cameraunlock::camera::FovZoomFactor(
        std::tan(drawnFovYDegrees * kHalfDegreesToRadians), tanBaseY);
    if (!std::isfinite(factor) || factor <= 0.0f) return scale;

    scale.known = true;
    scale.factor = factor;
    return scale;
}

// The vertical angle the slider's horizontal one comes to, for the log line that
// has to show a reader both halves in the same axis.
inline float SliderFovAsVerticalDegrees(float sliderFovXDegrees) {
    return 2.0f * std::atan(std::tan(sliderFovXDegrees * kHalfDegreesToRadians) /
                            kEngineReferenceAspect) *
           kRadiansToDegrees;
}

}  // namespace gc_ht
