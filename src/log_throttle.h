// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include <windows.h>

#include <atomic>

namespace gc_ht {

// The per-frame log schedule the render-view diagnostic runs on: a burst of
// unconditional opening lines, then a dense interval while the run is still
// young, then a thin one for the rest of the session.
//
// The opening burst is where an install-time fault shows (a wrong offset, a
// profile that does not fit). The thin steady trickle is what a late report is
// read from - "it drifted after an hour", "it stopped when I loaded a save" -
// which a burst that goes silent cannot see. Thinning is also what keeps the
// render thread out of the log mutex, and what bounds the file: at the steady
// tier a 144Hz session writes a line every 14 seconds, not one per frame.
//
// Counted in two currencies on purpose: `m_frame` advances every call so the
// intervals are frames, while `m_lines` advances only when a line is actually
// emitted, so the tiers are lines. Render-thread only, like its caller.
//
// Unsigned because `m_frame` advances once per rendered frame for the whole
// session and nothing bounds it. Signed overflow is undefined, and past the
// wrap `m_frame % interval` would go negative and the steady trickle - the part
// a late report is read from - would stop. Unsigned wrap is defined and the
// modulo stays in range.
class LogThrottle {
public:
    // `burst` opening lines are emitted unconditionally. Until `earlyLines`
    // have been emitted the interval is `earlyIntervalFrames`, and from then on
    // `steadyIntervalFrames`. Passing burst == earlyLines is the two-tier case.
    constexpr LogThrottle(unsigned burst, unsigned earlyLines, unsigned earlyIntervalFrames,
                          unsigned steadyIntervalFrames)
        : m_burst(burst),
          m_earlyLines(earlyLines),
          m_earlyIntervalFrames(earlyIntervalFrames),
          m_steadyIntervalFrames(steadyIntervalFrames) {}

    // Call once per frame from the path being logged. True on the frames whose
    // line should be written.
    bool ShouldLog() {
        ++m_frame;
        if (m_lines < m_burst) {
            ++m_lines;
            return true;
        }
        const unsigned interval =
            m_lines < m_earlyLines ? m_earlyIntervalFrames : m_steadyIntervalFrames;
        if (m_frame % interval != 0) return false;
        ++m_lines;
        return true;
    }

private:
    const unsigned m_burst;
    const unsigned m_earlyLines;
    const unsigned m_earlyIntervalFrames;
    const unsigned m_steadyIntervalFrames;

    unsigned m_frame = 0;
    unsigned m_lines = 0;
};

// The same job for a path that is not counted in frames: at most one line per
// interval, whichever thread gets there first.
//
// Wall clock rather than a frame count because the interface hooks run on
// whichever thread the engine draws that element on, and how many times either
// is called per frame is the engine's business. Atomic for the same reason: two
// threads inside one of these at once is ordinary, and the compare-exchange is
// what stops both writing the line.
//
// Separate from LogThrottle above, which is render-thread-only, unsynchronised
// and tiered. Neither is a generalisation of the other and folding them together
// would buy an atomic on the render thread's hot path for nothing.
class TimedLogGate {
public:
    explicit constexpr TimedLogGate(ULONGLONG intervalMs) : m_intervalMs(intervalMs) {}

    // True at most once per interval. The first call is always true, because
    // GetTickCount64 is never zero once the machine has been up a moment and an
    // opening line is what says the path is reached at all.
    bool ShouldLog() {
        const ULONGLONG now = GetTickCount64();
        ULONGLONG previous = m_lastMs.load(std::memory_order_relaxed);
        return now - previous >= m_intervalMs &&
               m_lastMs.compare_exchange_strong(previous, now, std::memory_order_relaxed);
    }

private:
    const ULONGLONG m_intervalMs;
    std::atomic<ULONGLONG> m_lastMs{0};
};

}  // namespace gc_ht
