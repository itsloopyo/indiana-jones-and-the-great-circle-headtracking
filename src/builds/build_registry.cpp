// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "builds/build_registry.h"

#include <windows.h>

#include "logging.h"

namespace gc_ht::builds {

extern const BuildProfile kGdkProfile_20260527;

namespace {

// Newest first. The head of this array is the diagnostic primary: when nothing
// matches, it is what the running EXE is compared against to say whether the
// game is newer or older than anything this mod knows about.
const BuildProfile* const kKnownProfiles[] = {
    &kGdkProfile_20260527,
};

void ReportUnknownBuild(const cameraunlock::memory::PeFingerprint& running) {
    const BuildProfile* primary = kKnownProfiles[0];
    switch (cameraunlock::memory::ClassifyMismatch(running, primary->fingerprint)) {
        case cameraunlock::memory::FingerprintMismatch::Newer:
            Log::Line("[build] this TheGreatCircle.exe is newer than any build this mod knows "
                      "about (newest known %s 0x%08X). Head tracking is off and the game is "
                      "unmodified; check the releases page for an updated mod.",
                      primary->name, primary->fingerprint.TimeDateStamp);
            break;
        case cameraunlock::memory::FingerprintMismatch::Older:
            Log::Line("[build] this TheGreatCircle.exe is older than the newest build this mod "
                      "knows about (newest known %s 0x%08X). Head tracking is off and the game "
                      "is unmodified; let the store finish updating.",
                      primary->name, primary->fingerprint.TimeDateStamp);
            break;
        case cameraunlock::memory::FingerprintMismatch::Differs:
            Log::Line("[build] this TheGreatCircle.exe has the expected build date but a "
                      "different size or checksum (expected SizeOfImage 0x%08X CheckSum 0x%08X). "
                      "The mod does not engage on a modified binary.",
                      primary->fingerprint.SizeOfImage, primary->fingerprint.CheckSum);
            break;
    }
    // The whole running triple, on every branch. A profile is routed by all
    // three fields, so a report that carries only the build date cannot be
    // turned into the new profile entry that the report exists to prompt - and
    // the answer is then a second round trip asking the player to run a
    // fingerprint tool. Two of the three used to be printed on one branch only,
    // and it was the branch that needs them least.
    Log::Line("[build] running TimeDateStamp 0x%08X SizeOfImage 0x%08X CheckSum 0x%08X",
              running.TimeDateStamp, running.SizeOfImage, running.CheckSum);
}

}  // namespace

// One read of the PE header, one walk of the registry, one line. Reading the
// header a second time to write the line is what lets the two disagree: a read
// that succeeds once and then refuses leaves the log naming a profile the mod
// is not installing against, and that line is the first thing a report is
// triaged from.
const BuildProfile* ResolveRunningBuild() {
    cameraunlock::memory::PeFingerprint running{};
    if (!cameraunlock::memory::ReadPeFingerprint(GetModuleHandleW(nullptr), running)) {
        Log::Line("[build] could not read the PE header of the running EXE; staying dormant");
        return nullptr;
    }

    for (const BuildProfile* profile : kKnownProfiles) {
        if (!running.Matches(profile->fingerprint)) continue;
        Log::Line("[build] matched %s (TimeDateStamp 0x%08X SizeOfImage 0x%08X CheckSum 0x%08X)",
                  profile->name, running.TimeDateStamp, running.SizeOfImage, running.CheckSum);
        return profile;
    }

    ReportUnknownBuild(running);
    return nullptr;
}

}  // namespace gc_ht::builds
