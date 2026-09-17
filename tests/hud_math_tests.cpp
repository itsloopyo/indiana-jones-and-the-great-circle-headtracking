// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "hud_math.h"
#include "check_harness.h"
#include <limits>

using checks::Check;
using checks::CheckNear;
using namespace gc_ht;

int main() {
    constexpr float degrees = 0.01745329252f;
    const idtech::Mat3 clean{{1,0,0, 0,1,0, 0,0,1}};
    float x = 0, y = 0;
    Check(ProjectHudAim(clean, clean, 90, 60, x, y), "centred aim is visible");
    CheckNear(x, 0, 1e-5f, "centred x");
    CheckNear(y, 0, 1e-5f, "centred y");

    auto turned = idtech::RotateBasisLocal(clean, -20 * degrees, 0, 0);
    Check(ProjectHudAim(clean, turned, 90, 60, x, y), "right turn keeps aim visible");
    CheckNear(x, -std::tan(20 * degrees), 1e-5f, "right turn moves aim left");
    CheckNear(y, 0, 1e-5f, "a pure yaw moves the aim horizontally and only horizontally");
    turned = idtech::RotateBasisLocal(clean, 20 * degrees, 0, 0);
    Check(ProjectHudAim(clean, turned, 90, 60, x, y), "left turn keeps aim visible");
    CheckNear(x, std::tan(20 * degrees), 1e-5f, "left turn moves aim right");
    CheckNear(y, 0, 1e-5f, "a pure yaw the other way is horizontal too");
    turned = idtech::RotateBasisLocal(clean, 0, 15 * degrees, 0);
    Check(ProjectHudAim(clean, turned, 90, 60, x, y), "upward turn keeps aim visible");
    CheckNear(y, -std::tan(15 * degrees) / std::tan(30 * degrees), 1e-5f,
              "upward turn moves aim down");
    CheckNear(x, 0, 1e-5f, "a pure pitch moves the aim vertically and only vertically");
    turned = idtech::RotateBasisLocal(clean, 0, 0, 35 * degrees);
    Check(ProjectHudAim(clean, turned, 90, 60, x, y), "roll keeps forward aim visible");
    CheckNear(x, 0, 1e-5f, "roll preserves centred x");
    CheckNear(y, 0, 1e-5f, "roll preserves centred y");

    // The combined pose, against the closed form of the composition rather than
    // against the matrix the implementation has just produced. This check used
    // to read its expectation out of `turned` itself - x against
    // -turned.m[3] / turned.m[0] - which is the definition of what
    // ProjectAimToNdc computes, so it passed for ANY basis whatsoever, including
    // one built with the roll and yaw factors multiplied in the wrong order.
    // That swap is invisible on every single-axis pose above, which is the whole
    // reason a combined case exists.
    //
    // RotateBasisLocal maps old-basis coordinates by L = Lr * Lp * Ly, so with an
    // identity clean basis the aim (1, 0, 0) lands at
    //   depth = cp*cy,  right = -L[1][0] = cr*sy - sr*sp*cy,
    //   up    =  L[2][0] = -sr*sy - cr*sp*cy
    // and NDC is each of the last two over the depth and its half-field tangent.
    {
        const float yaw = -20 * degrees, pitch = 10 * degrees, roll = 15 * degrees;
        const float cy = std::cos(yaw), sy = std::sin(yaw);
        const float cp = std::cos(pitch), sp = std::sin(pitch);
        const float cr = std::cos(roll), sr = std::sin(roll);
        const float depth = cp * cy;
        const float expectX = (cr * sy - sr * sp * cy) / depth / std::tan(45 * degrees);
        const float expectY = (-sr * sy - cr * sp * cy) / depth / std::tan(30 * degrees);
        turned = idtech::RotateBasisLocal(clean, yaw, pitch, roll);
        Check(ProjectHudAim(clean, turned, 90, 60, x, y), "combined pose visible");
        CheckNear(x, expectX, 1e-5f, "combined pose projects world aim x");
        CheckNear(y, expectY, 1e-5f, "combined pose projects world aim y");
    }

    // Pitch combined with roll, the litmus the doctrine names. This composition
    // puts roll outermost, so once the head tilts the pitch axis stops being
    // screen-vertical and the offset has to ROTATE with the roll. Checking the
    // radial magnitude in half-field units as well as the two components is what
    // separates "rotates correctly" from "drifts horizontally", which is the
    // failure that ships because it looks right on every single-axis test.
    {
        const float pitch = 25 * degrees;
        const float rolls[] = {0.0f, 35 * degrees, -35 * degrees};
        for (const float roll : rolls) {
            turned = idtech::RotateBasisLocal(clean, 0, pitch, roll);
            Check(ProjectHudAim(clean, turned, 90, 60, x, y), "pitch with roll stays visible");
            CheckNear(x, -std::sin(roll) * std::tan(pitch) / std::tan(45 * degrees), 1e-5f,
                      "pitch with roll swings the aim sideways by the roll");
            CheckNear(y, -std::cos(roll) * std::tan(pitch) / std::tan(30 * degrees), 1e-5f,
                      "pitch with roll keeps the vertical term on the cosine");
            const float rx = x * std::tan(45 * degrees), ry = y * std::tan(30 * degrees);
            CheckNear(std::sqrt(rx * rx + ry * ry), std::tan(pitch), 1e-5f,
                      "the offset rotates with roll rather than growing or shrinking");
        }
    }

    // World-yaw mode, looking straight down. Head yaw about world up is then a
    // pure spin about the view axis: the world turns under the player and the
    // aim never leaves the centre of the screen. A projection that treated head
    // yaw as camera-local would sweep the reticle out in an arc here.
    {
        const idtech::Mat3 down{{0, 0, -1, 0, 1, 0, 1, 0, 0}};
        turned = idtech::RotateBasisWorldYaw(down, 30 * degrees, 0, 0);
        Check(ProjectHudAim(down, turned, 90, 60, x, y), "world yaw looking down stays visible");
        CheckNear(x, 0, 1e-5f, "world yaw looking down leaves the aim at centre x");
        CheckNear(y, 0, 1e-5f, "world yaw looking down leaves the aim at centre y");
    }
    turned = idtech::RotateBasisLocal(clean, 100 * degrees, 0, 0);
    Check(!ProjectHudAim(clean, turned, 90, 60, x, y), "aim behind view is hidden");
    turned = idtech::RotateBasisLocal(clean, 55 * degrees, 0, 0);
    Check(!ProjectHudAim(clean, turned, 90, 60, x, y), "offscreen aim is hidden");
    Check(!ProjectHudAim(clean, clean, std::numeric_limits<float>::quiet_NaN(), 60, x, y),
          "invalid projection is rejected");

    const idtech::Vec3 initial{12,34,56};
    HudPose pose;
    pose.yaw = -20 * degrees;
    pose.pitch = 10 * degrees;
    pose.roll = 15 * degrees;
    pose.forward = 0.2f;
    pose.left = -0.1f;
    pose.up = 0.05f;
    auto axis = clean;
    auto origin = initial;
    ApplyHudPose(pose, origin, axis);
    CheckNear(origin.x, 12.2f, 1e-5f, "forward lean stays in body space after head yaw");
    CheckNear(origin.y, 33.9f, 1e-5f, "sideways lean stays in body space after head yaw");
    CheckNear(origin.z, 56.05f, 1e-5f, "vertical lean stays in body space after head pitch");

    // The basis ApplyHudPose produces, not just the origin. The three checks
    // above cannot see the rotation at all - the lean is applied first, in the
    // clean basis, so it is the same whatever the rotation does - which left the
    // one function whose whole job is to compose exactly as the camera hook does
    // with no check on the composing half. Both yaw modes, because which one is
    // taken is a config flag and the wrong branch is silent.
    // Against a basis that is NOT level, which is the whole point. With the
    // identity basis used above, the clean up row IS world +Z, so the two modes
    // produce the same matrix for every pose and this loop could not see which
    // branch ran - inverting the ternary in ApplyHudPose left the suite green.
    // Looking straight down separates them, exactly as it does for the reticle
    // case further up.
    const idtech::Mat3 tilted{{0, 0, -1, 0, 1, 0, 1, 0, 0}};
    const bool yawModes[] = {false, true};
    for (const bool worldYaw : yawModes) {
        HudPose posed = pose;
        posed.worldYaw = worldYaw;
        auto posedAxis = tilted;
        auto posedOrigin = initial;
        ApplyHudPose(posed, posedOrigin, posedAxis);
        const idtech::Mat3 expect =
            worldYaw ? idtech::RotateBasisWorldYaw(tilted, posed.yaw, posed.pitch, posed.roll)
                     : idtech::RotateBasisLocal(tilted, posed.yaw, posed.pitch, posed.roll);
        for (int i = 0; i < 9; ++i) {
            CheckNear(posedAxis.m[i], expect.m[i], 1e-5f,
                      worldYaw ? "world-yaw mode composes the basis the camera hook composes"
                               : "camera-local mode composes the basis the camera hook composes");
        }
    }

    // And the two modes genuinely differ on that basis, so the loop above is
    // comparing against two different matrices rather than one written twice.
    {
        const idtech::Mat3 world =
            idtech::RotateBasisWorldYaw(tilted, pose.yaw, pose.pitch, pose.roll);
        const idtech::Mat3 local =
            idtech::RotateBasisLocal(tilted, pose.yaw, pose.pitch, pose.roll);
        float spread = 0.0f;
        for (int i = 0; i < 9; ++i) spread += std::fabs(world.m[i] - local.m[i]);
        Check(spread > 0.1f, "the two yaw modes differ on a basis that is not level");
    }

    // The canvas placement, locked as it behaves today: NDC spans the full width
    // and height so half of each is the scale, y counts the other way from the
    // canvas, and a centred aim leaves the element exactly where its own layout
    // put it.
    HudPoint at = PlaceOnCanvas(100.0f, 50.0f, 0.0f, 0.0f, 1920.0f, 1080.0f);
    CheckNear(at.x, 100.0f, 1e-5f, "centred aim keeps the layout x");
    CheckNear(at.y, 50.0f, 1e-5f, "centred aim keeps the layout y");
    at = PlaceOnCanvas(100.0f, 50.0f, 1.0f, 1.0f, 1920.0f, 1080.0f);
    CheckNear(at.x, 100.0f + 960.0f, 1e-5f, "aim at the right edge moves half the canvas right");
    CheckNear(at.y, 50.0f - 540.0f, 1e-5f, "aim at the top edge moves half the canvas up");
    at = PlaceOnCanvas(0.0f, 0.0f, -0.5f, -0.25f, 800.0f, 600.0f);
    CheckNear(at.x, -200.0f, 1e-5f, "aim left of centre moves left");
    CheckNear(at.y, 75.0f, 1e-5f, "aim below centre moves down the canvas");

    return checks::Summarize("HUD math");
}
