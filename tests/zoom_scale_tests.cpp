// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "zoom_scale.h"

#include <initializer_list>
#include <limits>

#include "check_harness.h"

using checks::Check;
using checks::CheckNear;
using namespace gc_ht;

namespace {

// Written out here rather than taken from the shared gc_ht constant on
// purpose. The expectations below are what the conversion has to come out to,
// and computing them from the same constant the conversion uses would check the
// formula against itself.
constexpr float kIndependentHalfDegreesToRadians = 0.00872664626f;

// Where an angle lands up or down the frame, as a fraction of half the height.
// This is the quantity the whole conversion exists to hold still, so the tests
// are written in it rather than in the factor.
float ScreenFractionOfAngle(float angleDegrees, float fovYDegrees) {
    return std::tan(angleDegrees * kIndependentHalfDegreesToRadians * 2.0f) /
           std::tan(fovYDegrees * kIndependentHalfDegreesToRadians);
}

// Where a lean lands, at a fixed depth. The depth cancels out of every
// comparison below, so it is simply carried.
float ScreenFractionOfLean(float metres, float fovYDegrees, float depthMetres) {
    return metres / (depthMetres * std::tan(fovYDegrees * kIndependentHalfDegreesToRadians));
}

}  // namespace

int main() {
    // The relationship measured in game, to the precision the log prints it at:
    // g_fov 90 horizontal is drawn as 58.7155 vertical, in every window the game
    // was run in.
    CheckNear(SliderFovAsVerticalDegrees(90.0f), 58.7155f, 1e-3f,
              "g_fov 90 is 58.7155 degrees vertical");

    // The gate the doctrine names: ordinary play, nothing zoomed, factor 1.
    const ZoomScale none = ComputeZoomScale(58.7155f, 90.0f);
    Check(none.known, "an un-zoomed frame yields a usable factor");
    CheckNear(none.factor, 1.0f, 1e-4f, "an un-zoomed frame scales by exactly 1");
    CheckNear(none.Angle(17.5f), 17.5f, 1e-3f, "an un-zoomed angle is untouched");
    CheckNear(none.Length(0.3f), 0.3f, 1e-4f, "an un-zoomed lean is untouched");

    // The pairing the mod must NOT make: the DRAWN HORIZONTAL angle against the
    // slider's horizontal one. It reads 1.0 here only because this game
    // pillarboxes to 16:9, and this says what it would cost on a build that did
    // not - every pose in ordinary play at twice strength on a 32:9 viewport,
    // with no symptom beyond the tracking feeling too strong everywhere.
    const float tanDrawnHalfXAt32x9 =
        std::tan(58.7155f * kIndependentHalfDegreesToRadians) * (32.0f / 9.0f);
    CheckNear(tanDrawnHalfXAt32x9 / std::tan(90.0f * kIndependentHalfDegreesToRadians), 2.0f,
              1e-3f,
              "pairing the drawn horizontal with g_fov would double the pose at 32:9");
    CheckNear(ComputeZoomScale(58.7155f, 90.0f).factor, 1.0f, 1e-4f,
              "the anchored vertical stays 1.0 whatever the viewport is");

    // Aiming. Half the field of view is a little over twice the magnification.
    const ZoomScale scoped = ComputeZoomScale(SliderFovAsVerticalDegrees(45.0f), 90.0f);
    Check(scoped.known, "a narrowed frame yields a usable factor");
    CheckNear(scoped.factor,
              std::tan(45.0f * kIndependentHalfDegreesToRadians) /
                  std::tan(90.0f * kIndependentHalfDegreesToRadians),
              1e-4f, "the factor is the ratio of the half-angle tangents");
    Check(scoped.factor < 1.0f, "a narrowed frame scales the pose down");

    // The narrowing measured in game while the revolver comes up: g_fov 90 drawn
    // at 88.4211 horizontal, which the log reported as 0.9728.
    const ZoomScale measured = ComputeZoomScale(SliderFovAsVerticalDegrees(88.4211f), 90.0f);
    CheckNear(measured.factor, 0.9728f, 1e-3f, "the narrowing measured in game is 0.9728");

    // The property the whole conversion is for: a head angle and a lean move the
    // picture by the same fraction of the frame whatever the game zooms to.
    const float baseY = SliderFovAsVerticalDegrees(90.0f);
    for (const float drawnX : {90.0f, 70.0f, 45.0f, 20.0f}) {
        const float drawnY = SliderFovAsVerticalDegrees(drawnX);
        const ZoomScale zoom = ComputeZoomScale(drawnY, 90.0f);
        for (const float head : {2.0f, 10.0f, 25.0f}) {
            CheckNear(ScreenFractionOfAngle(zoom.Angle(head), drawnY),
                      ScreenFractionOfAngle(head, baseY), 1e-4f,
                      "a head angle moves the picture by the same fraction at any zoom");
            CheckNear(ScreenFractionOfLean(zoom.Length(0.3f), drawnY, 2.0f),
                      ScreenFractionOfLean(0.3f, baseY, 2.0f), 1e-4f,
                      "a lean moves the picture by the same fraction at any zoom");
        }
    }

    // A wider frame than the slider - a cinematic pull-out - scales the other way
    // rather than being clamped at 1.
    const ZoomScale widened = ComputeZoomScale(SliderFovAsVerticalDegrees(110.0f), 90.0f);
    Check(widened.known && widened.factor > 1.0f, "a widened frame scales the pose up");

    // The slider itself moving is the same arithmetic: a player on 100 gets a
    // factor of 1 in ordinary play, not a factor measured against a baked 90.
    const ZoomScale slider = ComputeZoomScale(SliderFovAsVerticalDegrees(100.0f), 100.0f);
    CheckNear(slider.factor, 1.0f, 1e-4f, "the player's own slider value is the reference");

    // Everything that is not a projection leaves the pose alone rather than
    // scaling it by a guess. Both numbers come out of game memory, so this is the
    // boundary check.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (const auto& bad : {ComputeZoomScale(nan, 90.0f), ComputeZoomScale(58.7f, nan),
                            ComputeZoomScale(inf, 90.0f), ComputeZoomScale(58.7f, inf),
                            ComputeZoomScale(0.0f, 90.0f), ComputeZoomScale(58.7f, 0.0f),
                            ComputeZoomScale(-45.0f, 90.0f), ComputeZoomScale(58.7f, 180.0f)}) {
        Check(!bad.known, "an unreadable field of view is not a factor");
        CheckNear(bad.factor, 1.0f, 1e-6f, "an unreadable field of view leaves the factor at 1");
        CheckNear(bad.Angle(12.0f), 12.0f, 1e-6f,
                  "an unreadable field of view passes an angle through");
        CheckNear(bad.Length(0.25f), 0.25f, 1e-6f,
                  "an unreadable field of view passes a lean through");
    }

    // A pose past a quarter turn keeps its sign. ScaleAngleForZoom is a tangent
    // round trip, and tan is negative in the second quadrant while atan answers
    // in the first, so an unguarded 100 degrees came back as -80 - the view
    // flipping half a turn between two frames as the head swept through 90. It
    // did that at factor 1.0, which is ordinary play, and a tracker profile
    // amplified for shoulder checks reaches there.
    //
    // Checked at a real zoom as well as at the un-zoomed reference, because the
    // guard has to be on the angle rather than on the factor.
    const ZoomScale wide = ComputeZoomScale(SliderFovAsVerticalDegrees(90.0f), 90.0f);
    const ZoomScale zoomed = ComputeZoomScale(SliderFovAsVerticalDegrees(45.0f), 90.0f);
    Check(wide.known && zoomed.known, "both reference scales are readable");
    Check(zoomed.factor < 0.6f, "a 45 degree zoom is a real narrowing");
    const float past[] = {90.5f, 100.0f, 135.0f, 179.0f};
    for (const float angle : past) {
        Check(wide.Angle(angle) > 0.0f, "an angle past 90 keeps its sign un-zoomed");
        Check(wide.Angle(-angle) < 0.0f, "and past -90 the other way");
        Check(zoomed.Angle(angle) > 0.0f, "an angle past 90 keeps its sign while zoomed");
        Check(zoomed.Angle(-angle) < 0.0f, "and past -90 while zoomed");
    }

    // Nothing jumps at the join. The saturated branch adds the remainder back,
    // so the two branches agree exactly at the limit and a head sweeping through
    // it sees no step at all.
    CheckNear(zoomed.Angle(89.0f), zoomed.Angle(89.0001f), 1e-3f,
              "the angle is continuous across the join");
    Check(std::fabs(zoomed.Angle(88.9f) - zoomed.Angle(89.1f)) < 0.5f,
          "and stays continuous either side of it");

    // And an ordinary head pose is still scaled, so the guard has not turned the
    // whole conversion off.
    Check(zoomed.Angle(20.0f) < 19.0f, "an ordinary angle is still scaled by the zoom");
    CheckNear(wide.Angle(20.0f), 20.0f, 1e-3f, "and is untouched when nothing is zoomed");

    return checks::Summarize("zoom scale");
}
