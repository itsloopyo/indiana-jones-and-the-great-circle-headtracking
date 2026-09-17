// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include "cameraunlock/math/angle_utils.h"

namespace gc_ht {

// The three angle conversions this mod does at its boundaries, narrowed from
// core's double constants once so every call site is the same number.
//
// Named rather than written out because the half-angle one in particular was
// spelled as a bare 0.00872664626 in four separate files, and a projection whose
// tangent is taken at the wrong half of the angle is off by a constant - which
// reads exactly like a projection that is right, just weaker.

// Degrees to radians, for the pose the boundary carries in degrees and the basis
// rotations take in radians.
inline constexpr float kDegreesToRadians = static_cast<float>(cameraunlock::math::kDegToRad);

// Radians to degrees, for the log lines, which are read by people.
inline constexpr float kRadiansToDegrees = static_cast<float>(cameraunlock::math::kRadToDeg);

// Degrees to radians AND halved, because every field of view in this engine is
// carried as the FULL angle while every projection wants the tangent of half it.
inline constexpr float kHalfDegreesToRadians = kDegreesToRadians * 0.5f;

}  // namespace gc_ht
