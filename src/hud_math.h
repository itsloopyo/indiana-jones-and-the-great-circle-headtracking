// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include <cmath>

#include "angle_units.h"
#include "cameraunlock/rendering/aim_ndc_projection.h"
#include "idtech/idtech_math.h"

namespace gc_ht {

// The geometry behind the interface compensation, kept apart from the hooks that
// use it so it can be exercised without a running game -
// tests/hud_math_tests.cpp is that check. Nothing here touches engine memory or
// engine state: every function is a pure function of its arguments.

// One frame's head pose, in the units the camera hook publishes: radians for the
// rotation, world units forward / left / up for the lean.
struct HudPose {
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    float forward = 0.0f, left = 0.0f, up = 0.0f;
    bool worldYaw = true;
};

// Turns a clean view into the tracked one the frame was actually drawn from,
// composing exactly as HeadTrackingMod::BuildTrackedView does - the interface
// has to be reprojected through the SAME camera the player is looking through,
// and a second derivation of the composition is a second thing to drift.
//
// The lean is applied FIRST, and so measured in the clean basis, which is what
// makes it follow where the body faces rather than where the head is looking.
// Rotating first and translating along the rotated rows would swing the lean
// round with every glance.
inline void ApplyHudPose(const HudPose& pose, idtech::Vec3& origin, idtech::Mat3& axis) {
    origin = idtech::TranslateAlongBasis(origin, axis, pose.forward, pose.left, pose.up);
    axis = pose.worldYaw ? idtech::RotateBasisWorldYaw(axis, pose.yaw, pose.pitch, pose.roll)
                         : idtech::RotateBasisLocal(axis, pose.yaw, pose.pitch, pose.roll);
}

// Where the game's aim direction lands in the picture drawn from the tracked
// view, in NDC - x right, y up, both -1..1 across the frame.
//
// The game keeps aiming along the CLEAN forward while the frame is drawn from
// the tracked basis, so its fixed centre-screen reticle stops marking where
// shots go the moment the head turns. This is the offset that puts it back.
//
// A DIRECTION, not an impact point: with a lean the render eye and the shot eye
// are different points, and the parallax between them is not corrected here.
// The shipped INI says so in as many words.
//
// False when there is no honest screen position to draw at - the aim has turned
// past the edge of the frame, or behind it, or the angles were unreadable - and
// the caller hides the reticle rather than pinning it to an edge.
inline bool ProjectHudAim(const idtech::Mat3& cleanAxis, const idtech::Mat3& trackedAxis,
                          float fovXDegrees, float fovYDegrees, float& ndcX, float& ndcY) {
    // id Tech's basis rows are forward / left / up while the projection wants a
    // RIGHT vector, so the left row is negated on the way in.
    const float right[3] = {-trackedAxis.m[3], -trackedAxis.m[4], -trackedAxis.m[5]};
    return cameraunlock::rendering::ProjectAimToNdc(
               cleanAxis.m, trackedAxis.m, right, trackedAxis.m + 6,
               std::tan(fovXDegrees * kHalfDegreesToRadians),
               std::tan(fovYDegrees * kHalfDegreesToRadians), ndcX, ndcY) &&
           std::fabs(ndcX) <= 1.0f && std::fabs(ndcY) <= 1.0f;
}

// A position on the interface canvas, in the pixels the sprite setter takes.
struct HudPoint {
    float x = 0.0f;
    float y = 0.0f;
};

// The reticle's canvas position for an aim at `ndcX` / `ndcY`, measured from
// wherever the element's own layout puts it.
//
// Half the canvas because NDC spans -1..1 across the FULL width and height, and
// y is subtracted because NDC counts up the frame while the canvas counts down
// it.
inline HudPoint PlaceOnCanvas(float layoutX, float layoutY, float ndcX, float ndcY,
                              float canvasWidth, float canvasHeight) {
    return {layoutX + ndcX * canvasWidth * 0.5f, layoutY - ndcY * canvasHeight * 0.5f};
}

}  // namespace gc_ht
