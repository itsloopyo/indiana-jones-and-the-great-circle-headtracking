// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include <atomic>
#include <memory>
#include <string>

#include "config.h"
#include "idtech/idtech_math.h"
#include "published_triple.h"
#include "tracker_feed.h"
#include "tracking_gate.h"
#include "zoom_scale.h"

namespace gc_ht {

namespace builds { struct BuildProfile; }

class CameraHook;
class GameState;
class Hotkeys;

// Mod-level coordinator: owns the config, the tracker feed, the render hook and
// the hotkeys, and is the single object the render hook asks for the frame's
// pose. Constructed once and never destroyed.
class HeadTrackingMod {
public:
    HeadTrackingMod();
    ~HeadTrackingMod();

    HeadTrackingMod(const HeadTrackingMod&) = delete;
    HeadTrackingMod& operator=(const HeadTrackingMod&) = delete;

    void Initialize();

    // Once per rendered frame, from the render hook. Answers whether a pose is
    // published for this frame.
    //
    // `zoom` is the frame's field of view against the game's un-zoomed one, and
    // it is applied HERE rather than at the camera write so that everything
    // reading the published pose - the camera hook, the world marker
    // projection, the reticle and the log line - describes the same camera the
    // player is looking through.
    bool UpdateForFrame(const ZoomScale& zoom);
    bool BuildTrackedView(idtech::Vec3& origin, idtech::Mat3& axis) const;

    bool GetRotationRadians(float& yaw, float& pitch, float& roll) const;
    bool GetPositionOffset(float& forward, float& left, float& up) const;

    bool IsWorldSpaceYaw() const { return m_worldSpaceYaw.load(); }

    void ToggleEnabled();
    void ToggleYawMode();
    void CycleTrackingMode();

    const Config& GetConfig() const { return m_config; }

private:
    void LoadSettings();
    void InstallEngineHooks(const builds::BuildProfile& profile);
    void LogVerdictChange(GateReason reason);

    Config m_config;
    // Where HeadTracking.ini lives. Empty when the game directory could not be
    // resolved.
    std::string m_exeDir;
    std::atomic<bool> m_enabled{true};
    std::atomic<bool> m_worldSpaceYaw{true};

    TrackerFeed m_feed;

    std::unique_ptr<GameState> m_gameState;
    std::unique_ptr<CameraHook> m_cameraHook;
    std::unique_ptr<Hotkeys> m_hotkeys;

    GateReason m_lastReason = GateReason::Gameplay;
    const char* m_lastStateReason = nullptr;
    bool m_stateKnown = false;

    // Only the two reasons the resolver alternates between are recorded here.
    // Compared by address, because they are distinct string literals.
    static constexpr int kMaxSeenStateReasons = 4;
    const char* m_seenStateReasons[kMaxSeenStateReasons] = {};
    int m_seenStateReasonCount = 0;

    // This frame's pose in the units the camera hook wants: radians for the
    // rotation, world units forward / left / up for the position.
    PublishedTriple m_rotation;
    PublishedTriple m_position;
};

HeadTrackingMod& GetMod();

}  // namespace gc_ht
