// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "window_centering.h"

#include <windows.h>

#include <intrin.h>

#include <atomic>
#include <cwchar>

#include "cameraunlock/hooks/hook_manager.h"
#include "logging.h"
#include "window_placement.h"

namespace gc_ht {

namespace {

constexpr int kPollIntervalMs = 250;
constexpr int kPollAttempts = 240;  // 60s, which covers a cold start off a hard disk.

// The rect has to hold still before it is worth acting on. The window is up
// before the engine has finished sizing and placing it, and nobody has measured
// when this game stops moving it, so the wait is on three seconds of an
// unchanged rect rather than on a fixed delay that would be a guess.
constexpr int kSettlePolls = 12;

// The render window's class. The window TITLE is "TheGreatCircle", which is not
// the same string, and the process owns two other kinds of window as well: a
// "TheGreatCircle WinConsole" and a pair of temp_d3d_window_*. An exact class
// match is what keeps this off all of them - core's FindGameWindow takes the
// first visible unowned window of the process instead, which is a coin toss
// between them and would burn the one move on the wrong one.
constexpr wchar_t kRenderWindowClass[] = L"TheGreatCircle_CLASS";

struct FindState {
    DWORD pid = 0;
    HWND window = nullptr;
};

BOOL CALLBACK PickRenderWindow(HWND window, LPARAM lparam) {
    auto* state = reinterpret_cast<FindState*>(lparam);

    DWORD windowPid = 0;
    GetWindowThreadProcessId(window, &windowPid);
    if (windowPid != state->pid) return TRUE;
    if (!IsWindowVisible(window)) return TRUE;

    wchar_t className[64] = {};
    if (GetClassNameW(window, className, ARRAYSIZE(className)) == 0) return TRUE;
    if (std::wcscmp(className, kRenderWindowClass) != 0) return TRUE;

    state->window = window;
    return FALSE;
}

HWND FindRenderWindow() {
    FindState state;
    state.pid = GetCurrentProcessId();
    EnumWindows(PickRenderWindow, reinterpret_cast<LPARAM>(&state));
    return state.window;
}

bool IsRenderWindow(HWND window) {
    wchar_t className[64] = {};
    return GetClassNameW(window, className, ARRAYSIZE(className)) > 0 &&
           std::wcscmp(className, kRenderWindowClass) == 0;
}

using SetWindowPosFn = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);

std::atomic<SetWindowPosFn> g_originalSetWindowPos{nullptr};
std::uintptr_t g_gameStart = 0;
std::uintptr_t g_gameEnd = 0;

// Placements are logged on counts 1, 2, 4, 8... A game that asks once at startup
// says so in one line, and one that asks every frame is still readable.
std::atomic<long> g_centred{0};
std::atomic<long> g_filling{0};

bool ShouldLog(std::atomic<long>& counter, long& count) {
    count = ++counter;
    return (count & (count - 1)) == 0;
}

// Rewrites the game's placement to the centred one. The window is moved by the
// game's own call, so nothing here races the game for the window's position.
BOOL WINAPI HookedSetWindowPos(HWND window, HWND insertAfter, int x, int y, int cx, int cy,
                               UINT flags) {
    const SetWindowPosFn original = g_originalSetWindowPos.load(std::memory_order_acquire);
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    // The caller range is what separates the game placing its own window from the
    // window being dragged and from this mod's own centring call: only the game's
    // module is rewritten, so a window the player has moved stays where they put
    // it until the game itself asks for a different place.
    if ((flags & SWP_NOMOVE) != 0 || caller < g_gameStart || caller >= g_gameEnd ||
        !IsRenderWindow(window)) {
        return original(window, insertAfter, x, y, cx, cy, flags);
    }

    int width = cx;
    int height = cy;
    if ((flags & SWP_NOSIZE) != 0) {
        // MINIMISED ONLY, and only for the size. A minimised window's live rect
        // is the shell's placeholder - off the far top-left of the desktop and a
        // couple of hundred pixels across - so centring on it computes the origin
        // for a window that size and the restored window comes back well off
        // where it belongs. SetWindowPos on a minimised window is setting where
        // it comes back to, so rcNormalPosition is the rect this call is really
        // placing.
        //
        // Not used for the other states. rcNormalPosition is the PRE-MAXIMISE
        // rect for a maximised window, so reading it there would report a small
        // size, and the placement that used to pass through as too large for the
        // work area would start being centred with that small window's
        // arithmetic - which would break the borderless pass-through recorded in
        // .lab/NOTES.md as measured against the live rect.
        //
        // Extents only, never the origin: rcNormalPosition is in workspace
        // coordinates rather than screen coordinates, and only the width and
        // height survive that difference unchanged.
        RECT current = {};
        bool haveRect = false;
        if (IsIconic(window)) {
            WINDOWPLACEMENT placement = {};
            placement.length = sizeof(placement);
            haveRect = GetWindowPlacement(window, &placement) != FALSE;
            current = placement.rcNormalPosition;
        } else {
            haveRect = GetWindowRect(window, &current) != FALSE;
        }
        if (!haveRect) {
            Log::Line("[window] could not read the window rect: %lu; leaving the game's "
                      "placement as it is", GetLastError());
            return original(window, insertAfter, x, y, cx, cy, flags);
        }
        width = static_cast<int>(current.right - current.left);
        height = static_cast<int>(current.bottom - current.top);
    }

    const RECT requested = {x, y, x + width, y + height};
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(MonitorFromRect(&requested, MONITOR_DEFAULTTONEAREST), &info)) {
        Log::Line("[window] GetMonitorInfoW failed: %lu; leaving the game's placement as it is",
                  GetLastError());
        return original(window, insertAfter, x, y, cx, cy, flags);
    }

    POINT target = {};
    const Placement placement = DecidePlacement(requested, info.rcWork, info.rcMonitor, target);
    if (placement != Placement::Center) {
        long count = 0;
        if (ShouldLog(g_filling, count)) {
            // Which of the two it is, not "one of these". They call for opposite
            // responses: already centred is the mod correctly doing nothing,
            // while too large is a player whose window will never be centred
            // because their work area cannot hold it.
            if (placement == Placement::AlreadyCentered) {
                Log::Line("[window] the game placed its %dx%d window at (%d, %d), which is "
                          "already centred, so it is left there (placement %ld)",
                          width, height, x, y, count);
            } else {
                Log::Line("[window] the game placed its %dx%d window at (%d, %d), which does not "
                          "fit the %dx%d work area, so it is left there (placement %ld)",
                          width, height, x, y,
                          static_cast<int>(info.rcWork.right - info.rcWork.left),
                          static_cast<int>(info.rcWork.bottom - info.rcWork.top), count);
            }
        }
        return original(window, insertAfter, x, y, cx, cy, flags);
    }

    long count = 0;
    if (ShouldLog(g_centred, count)) {
        Log::Line("[window] the game placed its %dx%d window at (%d, %d); centred to (%d, %d) "
                  "(placement %ld)", width, height, x, y, static_cast<int>(target.x),
                  static_cast<int>(target.y), count);
    }
    return original(window, insertAfter, target.x, target.y, cx, cy, flags);
}

void CenterUnlessAlready(HWND window, const RECT& rect) {
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info)) {
        Log::Line("[window] GetMonitorInfoW failed: %lu; leaving placement alone", GetLastError());
        return;
    }

    const int width = static_cast<int>(rect.right - rect.left);
    const int height = static_cast<int>(rect.bottom - rect.top);
    const int left = static_cast<int>(rect.left);
    const int top = static_cast<int>(rect.top);

    POINT target = {};
    switch (DecidePlacement(rect, info.rcWork, info.rcMonitor, target)) {
        case Placement::AlreadyCentered:
            Log::Line("[window] %dx%d at (%d, %d) is already centred, leaving it alone", width,
                      height, left, top);
            return;
        case Placement::TooLargeForWorkArea:
            Log::Line("[window] %dx%d does not fit the %dx%d work area, leaving it alone", width,
                      height, static_cast<int>(info.rcWork.right - info.rcWork.left),
                      static_cast<int>(info.rcWork.bottom - info.rcWork.top));
            return;
        case Placement::Center:
            break;
    }

    if (!SetWindowPos(window, nullptr, target.x, target.y, 0, 0,
                      SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)) {
        Log::Line("[window] SetWindowPos failed: %lu", GetLastError());
        return;
    }
    Log::Line("[window] centred the %dx%d window at (%d, %d), moved from (%d, %d)", width, height,
              static_cast<int>(target.x), static_cast<int>(target.y), left, top);
}

// Waits for a render window whose rect has held still for kSettlePolls, writing
// it and that rect to the out-params. False when none settled in time, in which
// case the out-params are left alone.
bool WaitForSettledWindow(HWND& settled, RECT& settledRect) {
    RECT previous = {};
    bool havePrevious = false;
    int stablePolls = 0;
    // Held across polls. Finding it asks every top-level window on the desktop
    // for its process and class name, and the wait is 240 polls long - a minute
    // of that four times a second to keep arriving at the same HWND. A poll that
    // cannot use the handle drops it and the next one looks again, so a window
    // that goes away is still found when it comes back.
    HWND window = nullptr;

    for (int attempt = 0; attempt < kPollAttempts; ++attempt) {
        Sleep(kPollIntervalMs);

        if (window == nullptr) window = FindRenderWindow();
        RECT current = {};
        // A minimised window reports a rect off the far top-left of the desktop,
        // so centring on that would park the restored window somewhere
        // arbitrary. Waiting it out is the whole handling: the counter resets
        // and the poll runs on.
        if (!window || !IsWindowVisible(window) || IsIconic(window) ||
            !GetWindowRect(window, &current)) {
            window = nullptr;
            havePrevious = false;
            stablePolls = 0;
            continue;
        }

        if (havePrevious && EqualRect(&previous, &current)) {
            if (++stablePolls < kSettlePolls) continue;
            settled = window;
            settledRect = current;
            return true;
        }
        previous = current;
        havePrevious = true;
        stablePolls = 0;
    }
    return false;
}

}  // namespace

void StartWindowCentering(std::uintptr_t imageBase, std::uint32_t imageSize) {
    // The image range comes from the build profile that has already matched,
    // rather than from a second walk of the game's PE header here. That walk
    // read e_lfanew and SizeOfImage raw, with no MZ check, no PE signature check
    // and no SEH - a second, unguarded source of truth for a number the
    // fingerprint read safely moments earlier and then verified against the
    // profile. A mod required to stay dormant on an unrecognised binary has no
    // business parsing that binary's headers by hand.
    g_gameStart = imageBase;
    g_gameEnd = imageBase + imageSize;

    void* address =
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowPos"));
    if (address == nullptr) {
        Log::Line("[window] user32.dll does not export SetWindowPos, so the game's own window "
                  "placements are left alone");
        return;
    }

    using cameraunlock::hooks::HookManager;
    using cameraunlock::hooks::HookStatus;
    using cameraunlock::hooks::HookStatusToString;
    HookManager& hooks = HookManager::Instance();
    const HookStatus initStatus = hooks.Initialize();
    if (initStatus != HookStatus::Ok && initStatus != HookStatus::ErrorAlreadyInitialized) {
        Log::Line("[window] MinHook init failed (%s), so the game's own window placements are "
                  "left alone", HookStatusToString(initStatus));
        return;
    }

    SetWindowPosFn original = nullptr;
    const HookStatus createStatus =
        hooks.CreateHook(address, reinterpret_cast<void*>(&HookedSetWindowPos),
                         reinterpret_cast<void**>(&original));
    if (createStatus != HookStatus::Ok) {
        Log::Line("[window] could not hook SetWindowPos (%s), so the game's own window "
                  "placements are left alone", HookStatusToString(createStatus));
        return;
    }
    // After the status check, not before it. A failed CreateHook leaves
    // `original` null, and storing that unconditionally would overwrite a
    // working trampoline with null while its hook was still enabled - so the
    // next SetWindowPos in the process would call through a null pointer.
    g_originalSetWindowPos.store(original, std::memory_order_release);
    const HookStatus enableStatus = hooks.EnableHook(address);
    if (enableStatus != HookStatus::Ok) {
        Log::Line("[window] could not enable the SetWindowPos hook (%s), so the game's own "
                  "window placements are left alone", HookStatusToString(enableStatus));
        return;
    }
}

void CenterWindowWhenReady() {
    HWND window = nullptr;
    RECT rect = {};
    if (!WaitForSettledWindow(window, rect)) {
        Log::Line("[window] no render window settled within %ds, leaving placement alone",
                  kPollAttempts * kPollIntervalMs / 1000);
        return;
    }
    CenterUnlessAlready(window, rect);
}

}  // namespace gc_ht
