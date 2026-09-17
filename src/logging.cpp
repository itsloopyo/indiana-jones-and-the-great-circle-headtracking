// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "logging.h"

#include <windows.h>

#include <string>

#include "cameraunlock/os/module_paths.h"

namespace gc_ht {

void OpenLogFile() {
    const std::wstring dir = cameraunlock::os::HostExeDirectory();
    // Core returns empty rather than falling back to the current directory,
    // precisely so the log never lands somewhere the player will not find it.
    // The README tells the player to look next to TheGreatCircle.exe, and no log
    // at all is a better report than a log in a place nobody names.
    if (dir.empty()) {
        OutputDebugStringA("[GreatCircleHeadTracking] could not resolve the game directory; "
                           "no log will be written\n");
        return;
    }
    cameraunlock::logging::Open(dir + L"\\HeadTracking.log");
}

}  // namespace gc_ht
