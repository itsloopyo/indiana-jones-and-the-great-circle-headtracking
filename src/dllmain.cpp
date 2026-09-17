// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "pch.h"

#include <exception>

#include "cameraunlock/diagnostics/crash_handler.h"
#include "headtracking_mod.h"
#include "logging.h"

namespace {

// The mod's outermost frame. An exception leaving a thread entry point is
// std::terminate, which kills the game the mod is a guest in - so the whole of
// setup sits inside the guard, and a failure leaves a log line rather than a
// dead process. Not necessarily a dormant mod: see the catch below for what can
// already be running by then.
DWORD WINAPI BootstrapThread(LPVOID) {
    try {
        gc_ht::OpenLogFile();
        gc_ht::Log::Line("[main] GreatCircleHeadTracking %s loaded into pid %lu",
                         HEADTRACKING_VERSION_STRING, GetCurrentProcessId());
        // The catch below only covers this thread. The camera hook writes
        // through engine pointers on the game's own thread every frame, and a
        // fault there is otherwise a crash with nothing in HeadTracking.log to
        // say whether this mod was in the stack - which is the whole content of
        // a "the game crashes with the mod installed" report. Core's filter
        // chains to whatever the game installed, so its crash flow is unchanged.
        cameraunlock::diagnostics::InstallCrashHandler();
        gc_ht::GetMod().Initialize();
    } catch (const std::exception& e) {
        // Deliberately does NOT claim the game is unaffected. Initialize binds
        // the UDP port and installs the render detour before it builds the game
        // state and the hotkeys, so a throw from the later half leaves a hooked
        // engine, a bound socket and live receiver threads behind - and a line
        // saying the game is unmodified sends the next triage in the wrong
        // direction. What is live depends on how far it got, so the line says
        // what it knows and no more.
        gc_ht::Log::Line("[main] startup failed (%s); head tracking will not work. Anything "
                         "already started stays as it is - read the lines above this one to "
                         "see how far startup got.", e.what());
    } catch (...) {
        gc_ht::Log::Line("[main] startup failed; head tracking will not work. Anything already "
                         "started stays as it is - read the lines above this one to see how far "
                         "startup got.");
    }
    return 0;
}

// Takes a permanent reference on this module, so an unload cannot pull the code
// out from under the bootstrap thread or the installed hook.
void PinSelf() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                       reinterpret_cast<LPCWSTR>(&BootstrapThread), &self);
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID /*reserved*/) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(module);
            // Pinned before the thread that outlives DllMain is started: a
            // FreeLibrary while the bootstrap thread is still inside config
            // loading leaves that thread running in freed memory, which is a
            // crash in a module the debugger can only name "_unloaded".
            PinSelf();
            if (HANDLE thread = CreateThread(nullptr, 0, BootstrapThread, nullptr, 0, nullptr)) {
                CloseHandle(thread);
            } else {
                // The log file is opened by that thread, so a failure here
                // leaves no HeadTracking.log and nothing anywhere to explain an
                // ASI that loaded and did nothing. OutputDebugStringA is the one
                // diagnostic that is safe under the loader lock.
                OutputDebugStringA("[GreatCircleHeadTracking] could not start the bootstrap "
                                   "thread; the mod is dormant\n");
            }
            break;

        case DLL_PROCESS_DETACH:
            // Nothing. Not on process exit: the OS has already killed our worker
            // threads, possibly mid-syscall or holding the log mutex, so joining
            // or unhooking then can hang the game on the way out. And not on a
            // FreeLibrary either: DllMain runs under the loader lock, and joining
            // the hotkey and receiver threads needs that same lock for their own
            // exit path, which is a textbook deadlock. An ASI plugin is never
            // unloaded in practice; process teardown reclaims everything.
            break;
    }
    return TRUE;
}
