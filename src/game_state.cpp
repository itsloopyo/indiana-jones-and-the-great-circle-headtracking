// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "game_state.h"

#include <windows.h>

#include <atomic>
#include <cstring>

#include "cameraunlock/memory/safe_memory.h"
#include "idtech/singleton_sweep.h"
#include "logging.h"

namespace gc_ht {

namespace {

// gameFrameParms_t, from the engine's own field reflection tables:
//
//   +0x00 int  renderWidth        +0x10 int  scanoutWidth
//   +0x04 int  renderHeight       +0x14 int  scanoutHeight
//   +0x08 int  upsampleWidth      +0x18 float pixelAspect
//   +0x0C int  upsampleHeight     +0x25 bool doRender
//   +0x28 bool isLoading          +0x2D bool advanceTime
constexpr std::uint32_t kFpRenderWidth = 0x00;
constexpr std::uint32_t kFpRenderHeight = 0x04;
constexpr std::uint32_t kFpUpsampleWidth = 0x08;
constexpr std::uint32_t kFpUpsampleHeight = 0x0C;
constexpr std::uint32_t kFpScanoutWidth = 0x10;
constexpr std::uint32_t kFpScanoutHeight = 0x14;
constexpr std::uint32_t kFpPixelAspect = 0x18;
constexpr std::uint32_t kFpFirstBool = 0x25;
constexpr std::uint32_t kFpIsLoading = 0x28;
constexpr std::uint32_t kFpAdvanceTime = 0x2D;
constexpr std::uint32_t kFpLastBool = 0x31;
constexpr std::size_t kFrameParmsWindow = 0x40;

template <typename T>
T Load(const unsigned char* p, std::uint32_t offset) {
    T value{};
    std::memcpy(&value, p + offset, sizeof(T));
    return value;
}

bool IsBoolByte(const unsigned char* p, std::uint32_t offset) {
    return p[offset] <= 1;
}

bool PlausibleExtent(std::int32_t v) { return v >= 16 && v <= 32768; }

// The frame block carries three independent width/height pairs and then a run
// of nine bools. Three pairs of plausible extents is already rare; a run of
// bytes that are all 0 or 1 immediately after them is not something a float
// array or a string buffer produces.
bool LooksLikeFrameParms(const unsigned char* p) {
    if (!PlausibleExtent(Load<std::int32_t>(p, kFpScanoutWidth))) return false;
    if (!PlausibleExtent(Load<std::int32_t>(p, kFpScanoutHeight))) return false;
    if (!PlausibleExtent(Load<std::int32_t>(p, kFpRenderWidth))) return false;
    if (!PlausibleExtent(Load<std::int32_t>(p, kFpRenderHeight))) return false;
    if (!PlausibleExtent(Load<std::int32_t>(p, kFpUpsampleWidth))) return false;
    if (!PlausibleExtent(Load<std::int32_t>(p, kFpUpsampleHeight))) return false;

    const float aspect = Load<float>(p, kFpPixelAspect);
    if (!(aspect > 0.25f && aspect < 4.0f)) return false;

    for (std::uint32_t o = kFpFirstBool; o <= kFpLastBool; ++o) {
        if (!IsBoolByte(p, o)) return false;
    }
    // The renderer never upsamples below the render size, and never scans out
    // below the upsample size. Two orderings a random block of ints passes only
    // by luck.
    if (Load<std::int32_t>(p, kFpUpsampleWidth) < Load<std::int32_t>(p, kFpRenderWidth)) return false;
    if (Load<std::int32_t>(p, kFpScanoutWidth) < Load<std::int32_t>(p, kFpUpsampleWidth)) return false;
    return true;
}

// Says once what the sweep settled on. A content sweep can land on a block that
// merely looks right, and printing the address is the least that makes that
// visible in a log rather than invisible in behaviour.
void ReportResolved(const char* what, void* object) {
    Log::Line("[state] resolved %s at %p", what, object);
}

}  // namespace

GameState::GameState() {
    m_frameParms.Start([] {
        bool cutShort = false;
        void* found = idtech::SweepCommittedMemory(
            [](const unsigned char* p, std::uintptr_t) { return LooksLikeFrameParms(p); },
            kFrameParmsWindow, &cutShort);
        // A walk that VirtualQuery stopped early covered a prefix of the address
        // space, so it can miss a block that is there. Without this line a
        // systematically truncated sweep is indistinguishable from one running
        // to completion and finding nothing, and the gate reports "still
        // locating" for the rest of the session either way.
        //
        // Latched, because the worker retries every three seconds for as long as
        // nothing is published and a VirtualQuery refusal is systematic rather
        // than transient - so an unlatched line would be a thousand an hour in
        // the one file a "no head tracking" report is read from.
        static std::atomic<bool> s_reported{false};
        if (cutShort && !s_reported.exchange(true)) {
            Log::Line("[state] the memory sweep was cut short before it finished");
        }
        return found;
    });
}

// The two literals, named once so IsGameplay and ReasonIsResolverChurn cannot
// drift apart. Compared by address below, which is what the caller's own
// bookkeeping relies on.
namespace {
const char* const kStillLocating = "still locating the engine's frame state";
const char* const kUnreadable = "the frame state could not be read";
}  // namespace

bool GameState::ReasonIsResolverChurn() const {
    return m_reason == kStillLocating || m_reason == kUnreadable;
}

bool GameState::IsGameplay() {
    void* parms = m_frameParms.Published();

    if (parms != nullptr && parms != m_reportedParms) {
        m_reportedParms = parms;
        ReportResolved("gameFrameParms_t", parms);
    }

    // Failing toward stock while nothing has been resolved. A frame the mod
    // cannot place is a frame it does not move: the alternative is a camera that
    // swims through the loading screen on every launch for the seconds the sweep
    // takes.
    if (parms == nullptr) {
        m_reason = kStillLocating;
        return false;
    }

    const auto parmsBase = reinterpret_cast<std::uintptr_t>(parms);
    std::uint8_t loading = 0;
    std::uint8_t advance = 0;
    if (!cameraunlock::memory::SafeRead(parmsBase + kFpIsLoading, loading) ||
        !cameraunlock::memory::SafeRead(parmsBase + kFpAdvanceTime, advance)) {
        // Clearing it is what asks the worker to sweep again. A block that has
        // become unreadable was freed under us.
        m_frameParms.Clear();
        m_reason = kUnreadable;
        return false;
    }
    if (loading != 0) {
        m_reason = "loading";
        return false;
    }
    // advanceTime is false whenever the engine is holding game time still, which
    // is the pause menu, an inventory screen and an alt-tab alike. It is NOT
    // false on the main menu, which runs its own live scene - so head tracking
    // is not suppressed there. See game_state.h for why that gap is left open
    // rather than closed with a flag that cannot be found reliably.
    if (advance == 0) {
        m_reason = "paused";
        return false;
    }

    m_reason = "gameplay";
    return true;
}

}  // namespace gc_ht
