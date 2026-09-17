// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
//
// Locks the decision the window centring makes about a settled window: move it
// only when the game is windowed and has not centred it already. The part that
// touches Win32 needs a running game to exercise; this part does not, and it is
// the part that decides whether a player's window gets moved when it should
// have been left alone.

#include "window_placement.h"

#include "check_harness.h"

namespace {

using checks::Check;

using gc_ht::DecidePlacement;
using gc_ht::Placement;

RECT Rect(int left, int top, int width, int height) {
    return RECT{left, top, left + width, top + height};
}

// A 1920x1080 monitor at the origin with a 40px taskbar along the bottom.
const RECT kMonitor = Rect(0, 0, 1920, 1080);
const RECT kWork = Rect(0, 0, 1920, 1040);

Placement Decide(const RECT& window, POINT& target) {
    return DecidePlacement(window, kWork, kMonitor, target);
}

void TestOffCentreWindowIsCentredOnTheWorkArea() {
    POINT target = {};
    Check(Decide(Rect(0, 0, 1280, 720), target) == Placement::Center,
          "a 1280x720 window in the top-left corner is moved");
    Check(target.x == 320 && target.y == 160, "it lands centred on the work area");
}

void TestAlreadyCentredWindowIsLeftAlone() {
    POINT target = {};
    Check(Decide(Rect(320, 160, 1280, 720), target) == Placement::AlreadyCentered,
          "a window already centred on the work area is left alone");
    // The game centres on the monitor, this mod centres on the work area, and
    // half the taskbar separates them. Both readings count as centred.
    Check(Decide(Rect(320, 180, 1280, 720), target) == Placement::AlreadyCentered,
          "a window centred on the monitor is left alone too");
}

void TestOnePixelOfRoundingIsNotAMove() {
    POINT target = {};
    Check(Decide(Rect(321, 161, 1280, 720), target) == Placement::AlreadyCentered,
          "a window one pixel off centre is left alone");
    Check(Decide(Rect(323, 160, 1280, 720), target) == Placement::Center,
          "a window three pixels off centre is moved");
}

void TestFullscreenAndBorderlessAreLeftAlone() {
    POINT target = {};
    Check(Decide(Rect(0, 0, 1920, 1080), target) == Placement::AlreadyCentered,
          "a window filling the monitor is left alone");
}

// A windowed 1920x1080 client area carries a title bar and border, so its window
// rect is wider and taller than the screen. Centring it would put the title bar
// off the top, out of reach of the mouse.
//
// Off centre on BOTH axes. The rect this used to use was exactly centred
// horizontally on the work area and the monitor alike, so only a 8px and 12px
// vertical difference kept it out of the AlreadyCentered early return - a change
// to kTolerance would have quietly moved it onto a different branch while the
// assertion went on naming this one.
void TestWindowLargerThanTheWorkAreaIsLeftAlone() {
    POINT target = {};
    Check(Decide(Rect(-40, -31, 1936, 1119), target) == Placement::TooLargeForWorkArea,
          "a window larger than the work area is left alone");
    Check(Decide(Rect(0, 0, 1936, 900), target) == Placement::TooLargeForWorkArea,
          "too wide alone is enough");
    Check(Decide(Rect(0, 0, 1000, 1119), target) == Placement::TooLargeForWorkArea,
          "too tall alone is enough");
}

// The geometry actually measured on the development machine, which is the only
// placement in this file with ground truth behind it: a 5120x1440 monitor with a
// 5120x1392 work area, the game asking to place its 1296x759 window at
// (3824, 421), and the borderless window that has to pass straight through.
// Everything else here is synthetic.
void TestTheMeasuredUltrawidePlacements() {
    const RECT monitor = Rect(0, 0, 5120, 1440);
    const RECT work = Rect(0, 0, 5120, 1392);
    POINT target = {};
    Check(DecidePlacement(Rect(3824, 421, 1296, 759), work, monitor, target) == Placement::Center,
          "the placement the game works out for itself is moved");
    Check(target.x == 1912 && target.y == 316, "it lands centred on the 5120x1392 work area");

    Check(DecidePlacement(Rect(0, 0, 5120, 1440), work, monitor, target) ==
              Placement::AlreadyCentered,
          "the borderless window that comes with r_fullscreen 2 passes straight through");
}

void TestSecondMonitorUsesItsOwnWorkArea() {
    // A monitor to the left of the primary, so its coordinates are negative.
    const RECT monitor = Rect(-1920, 0, 1920, 1080);
    const RECT work = Rect(-1920, 0, 1920, 1040);
    POINT target = {};
    Check(DecidePlacement(Rect(-1920, 0, 1280, 720), work, monitor, target) == Placement::Center,
          "a window on a monitor left of the primary is moved");
    Check(target.x == -1600 && target.y == 160, "it lands centred on that monitor's work area");
}

}  // namespace

int main() {
    TestOffCentreWindowIsCentredOnTheWorkArea();
    TestAlreadyCentredWindowIsLeftAlone();
    TestOnePixelOfRoundingIsNotAMove();
    TestFullscreenAndBorderlessAreLeftAlone();
    TestWindowLargerThanTheWorkAreaIsLeftAlone();
    TestTheMeasuredUltrawidePlacements();
    TestSecondMonitorUsesItsOwnWorkArea();

    return checks::Summarize("window placement");
}
