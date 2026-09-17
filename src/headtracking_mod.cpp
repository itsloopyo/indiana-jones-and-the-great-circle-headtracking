// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "headtracking_mod.h"

#include <windows.h>

#include "angle_units.h"
#include "builds/build_registry.h"
#include "camera_hook.h"
#include "cameraunlock/os/module_paths.h"
#include "game_state.h"
#include "hotkeys.h"
#include "logging.h"
#include "window_centering.h"

namespace gc_ht {

// Deliberately leaked, never destroyed. A function-local static would register
// the destructor with atexit, and MSVC runs those from LdrShutdownProcess AFTER
// DllMain(DLL_PROCESS_DETACH) - so the careful "do not tear down on process
// exit" guard in dllmain.cpp would be undone by the destructor doing exactly
// that, under the loader lock, joining threads the OS has already killed.
HeadTrackingMod& GetMod() {
    static HeadTrackingMod* instance = new HeadTrackingMod();
    return *instance;
}

HeadTrackingMod::HeadTrackingMod() = default;
HeadTrackingMod::~HeadTrackingMod() = default;

void HeadTrackingMod::LoadSettings() {
    // Core's narrowing, because WideCharToMultiByte best-fit maps by default: a
    // character the ANSI code page cannot encode becomes a similar-looking one,
    // so a game directory can narrow to the name of a DIFFERENT directory that
    // exists and the INI is then read from and written to that one. Core
    // refuses instead, which lands on the no-INI path below.
    m_exeDir = cameraunlock::os::HostExeDirectoryNarrow();
    if (m_exeDir.empty()) {
        Log::Line("[mod] could not resolve the game directory in a form the INI reader can "
                  "use; built-in defaults are in use and HeadTracking.ini will not be read");
        return;
    }
    WriteDefaultConfigIfMissing(m_exeDir);
    LoadConfig(m_exeDir, m_config);

    // The settings actually in force. Everything config.cpp writes is a
    // COMPLAINT - a value out of range, a key set twice, a file that would not
    // open - so a key the player misspelled, or an INI they edited beside a
    // different copy of the game, produces no line at all, and the log cannot
    // then tell a value that was read from one that was never seen. Naming the
    // directory is what catches the second case, which is ordinary on a machine
    // that has the game installed from two stores. Only the settings nothing
    // else reports: the port, the hotkeys, the smoothing pair and the startup
    // toggle each get a line from the subsystem that owns them.
    Log::Line("[config] in force beside %s: position %s, lean limits x=%.2f y=%.2f "
              "z=%.2f forward %.2f back, world markers %s",
              m_exeDir.c_str(), m_config.position_enabled ? "on" : "off", m_config.limit_x,
              m_config.limit_y, m_config.limit_z, m_config.limit_z_back,
              m_config.compensate_world_markers ? "on" : "off");
}

// A hook that will not install on a build the mod DOES recognise leaves the
// receiver and the hotkeys running, so a log inspection still shows whether
// tracking data is arriving. An unrecognised build never gets this far - see
// Initialize.
void HeadTrackingMod::InstallEngineHooks(const builds::BuildProfile& profile) {
    m_cameraHook = std::make_unique<CameraHook>();
    if (!m_cameraHook->Install(profile, *this)) {
        m_cameraHook.reset();
        return;
    }
    // Built only once the hook has installed, because building it starts two
    // threads that each read every committed writable region of the process
    // every three seconds until they find what they are looking for. With a
    // failed hook that would be running immediately after the line above told
    // the player the mod was dormant and the game unmodified.
    m_gameState = std::make_unique<GameState>();

    // And the detour is armed last, because it reads m_gameState on its very
    // first frame. Arming inside Install would race the assignment above.
    m_cameraHook->Arm();
}

void HeadTrackingMod::Initialize() {
    LoadSettings();

    m_enabled.store(m_config.enable_on_startup);
    m_worldSpaceYaw.store(m_config.world_space_yaw);

    // Everything below this line modifies the running process in some way a
    // player can notice: a bound UDP port, a key poller, a hooked engine
    // function. An unrecognised build gets none of it. The build-profile
    // doctrine is that a mismatch leaves the game running vanilla.
    const builds::BuildProfile* profile = builds::ResolveRunningBuild();
    if (profile == nullptr) return;

    m_feed.Start(m_config);
    InstallEngineHooks(*profile);

    m_hotkeys = std::make_unique<Hotkeys>();
    m_hotkeys->Start(*this, m_config);

    Log::Line("[mod] ready: tracking %s, yaw about %s",
              m_enabled.load() ? "on" : "off",
              m_worldSpaceYaw.load() ? "world up" : "the view axis");

    // Both halves of the window centring are below the dormant return above on
    // purpose - a build this mod has no profile for leaves the game entirely
    // alone, and moving the player's window would be the one thing it still did.
    //
    // The placement hook goes in first and returns immediately: it has to be live
    // before the game places its window the second time, about fifteen seconds
    // into startup.
    StartWindowCentering(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)),
                         profile->fingerprint.SizeOfImage);

    // And the wait is last, because it blocks for as long as the engine takes to
    // put its window up: the hook, the receiver and the hotkeys are all live
    // before this waits on anything.
    //
    // On this thread rather than the render hook's first frame: SetWindowPos on
    // a window owned by another thread blocks until that thread pumps the
    // message, and the game's main thread waits on its render thread often
    // enough that doing it from inside the hook is a deadlock waiting for a
    // frame that never comes.
    CenterWindowWhenReady();
}

void HeadTrackingMod::LogVerdictChange(GateReason reason) {
    // GameState's sub-reason is part of the key, not just part of the message.
    // NotGameplay covers four of them, and keying on the GateReason alone meant
    // the first one to occur silenced the rest for the session: a block that
    // became unreadable mid-session went on being reported as "loading" while
    // the sweep restarted forever. A pointer compare is enough - they are
    // distinct string literals.
    const char* const stateReason = m_gameState->LastReason();
    // The first evaluation is reported as well as every change. Reporting only
    // changes leaves a session that never reaches gameplay with nothing in the
    // log to say why - which is the one case a "no head tracking" report needs.
    if (m_stateKnown && reason == m_lastReason && stateReason == m_lastStateReason) return;
    m_stateKnown = true;
    m_lastReason = reason;
    m_lastStateReason = stateReason;

    // The two resolver reasons are reported once each; everything else is
    // reported on every change. Keying on the pair alone was unbounded: a frame
    // block that has been freed makes GameState clear it and the resolver
    // republish it every three seconds, so the sub-reason alternates between
    // "could not be read" and "still locating" for the rest of the session and
    // every flip logged.
    //
    // Bounding ALL of them instead is worse, and was the first attempt. The
    // sub-reason for a gameplay verdict is "gameplay", so the first one marks it
    // seen and no later frame can ever log that tracking resumed - a session
    // that loads once ends with "tracking suspended (loading)" as its last word
    // on the subject, which is the wrong answer to hand the next triage.
    if (m_gameState->ReasonIsResolverChurn()) {
        for (const char* seen : m_seenStateReasons) {
            if (seen == stateReason) return;
        }
        if (m_seenStateReasonCount < kMaxSeenStateReasons) {
            m_seenStateReasons[m_seenStateReasonCount++] = stateReason;
        }
    }

    // The gate's own words for everything it decided itself, and GameState's for
    // the one answer it did not: "not in gameplay" is true of the main menu, a
    // level load and the pause menu alike, and which of those it is is the whole
    // content of a "no head tracking" report.
    if (reason == GateReason::NotGameplay) {
        Log::Line("[state] tracking suspended (%s)", stateReason);
        return;
    }
    Log::Line("[state] %s (%s)", PoseApplies(reason) ? "gameplay" : "tracking suspended",
              GateReasonText(reason));
}

bool HeadTrackingMod::UpdateForFrame(const ZoomScale& zoom) {
    GateInputs inputs;
    inputs.gameplay = m_gameState->IsGameplay();
    inputs.enabled = m_enabled.load();

    const GateReason reason = EvaluateGate(inputs);
    LogVerdictChange(reason);

    const bool poseApplies = PoseApplies(reason);
    m_feed.Update(poseApplies);

    if (!poseApplies) {
        m_rotation.Invalidate();
        m_position.Invalidate();
        return false;
    }

    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    const bool rotationValid = m_feed.GetRotationDegrees(yaw, pitch, roll);
    // Yaw and pitch carry the zoom factor and roll does not - see ZoomScale. The
    // scaling is the last thing done to the pose, after the feed's own limits,
    // because what has to come out the same size at every field of view is the
    // distance the picture moves rather than the angle the head turned.
    m_rotation.Publish(zoom.Angle(yaw) * kDegreesToRadians,
                       zoom.Angle(pitch) * kDegreesToRadians, roll * kDegreesToRadians,
                       rotationValid);

    float forward = 0.0f, left = 0.0f, up = 0.0f;
    const bool positionValid = m_feed.GetPositionOffset(forward, left, up);
    m_position.Publish(zoom.Length(forward), zoom.Length(left), zoom.Length(up), positionValid);

    return rotationValid || positionValid;
}

bool HeadTrackingMod::BuildTrackedView(idtech::Vec3& origin, idtech::Mat3& axis) const {
    const idtech::Mat3 cleanAxis = axis;
    bool modified = false;
    float yaw, pitch, roll;
    if (GetRotationRadians(yaw, pitch, roll)) {
        axis = IsWorldSpaceYaw() ? idtech::RotateBasisWorldYaw(cleanAxis, yaw, pitch, roll)
                                 : idtech::RotateBasisLocal(cleanAxis, yaw, pitch, roll);
        modified = true;
    }
    float forward, left, up;
    if (GetPositionOffset(forward, left, up)) {
        // The CLEAN basis, not the rotated one. That is what makes a lean follow
        // where the body faces rather than where the head is looking: turn your
        // head and lean in, and you move along your shoulders' forward.
        origin = idtech::TranslateAlongBasis(origin, cleanAxis, forward, left, up);
        modified = true;
    }
    return modified;
}

bool HeadTrackingMod::GetRotationRadians(float& yaw, float& pitch, float& roll) const {
    return m_rotation.Read(yaw, pitch, roll);
}

bool HeadTrackingMod::GetPositionOffset(float& forward, float& left, float& up) const {
    return m_position.Read(forward, left, up);
}

// The toggles log from here rather than from the hotkey handler, so a nav key
// and its Ctrl+Shift chord produce the same single line - and so the log names
// the state that was actually reached, not the one the caller asked for.
void HeadTrackingMod::ToggleEnabled() {
    const bool next = !m_enabled.load();
    m_enabled.store(next);
    Log::Line("[mod] tracking -> %s", next ? "on" : "off");
}

void HeadTrackingMod::ToggleYawMode() {
    const bool next = !m_worldSpaceYaw.load();
    m_worldSpaceYaw.store(next);
    Log::Line("[mod] yaw about -> %s", next ? "world up" : "the view axis");
}

void HeadTrackingMod::CycleTrackingMode() {
    m_feed.CycleMode();
    Log::Line("[mod] tracking mode -> %s", m_feed.ModeName());
}

}  // namespace gc_ht
