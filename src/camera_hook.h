// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include <cstdint>

#include "builds/build_profile.h"
#include "hud_view.h"
#include "idtech/idtech_math.h"
#include "zoom_scale.h"

namespace gc_ht {

class HeadTrackingMod;

// Hooks idRenderWorldLocal::Render and injects the head pose into the render
// view the frame is built from.
//
// The injection is a sandwich around the original call: the game's own view is
// read out, the tracked view is written in its place, Render runs and derives
// the projection matrix, the frustum and every culling decision from it, and
// the game's view is put straight back. Nothing that runs later can observe the
// tracked pose, so aim, projectiles, traces and AI vision are identical with
// tracking on and off - and because the frustum is built AFTER the write,
// nothing is culled at the edges of a turned view, since the frustum turned too.
class CameraHook {
public:
    bool Install(const builds::BuildProfile& profile, HeadTrackingMod& mod);

    // Makes the detour start injecting. Separate from Install so the caller can
    // finish wiring up everything the first frame touches before a frame can
    // arrive. Until this is called the detour is a pass-through.
    void Arm();

private:
    // Five arguments were observed at the one call site that reaches this
    // function - rcx, rdx, r8, r9d and one stack slot at [rsp+0x20], with
    // nothing written above it. Three spare slots are declared and forwarded
    // anyway: forwarding more arguments than the callee reads is harmless,
    // dropping one it does read is a corrupted call, and the cost of being
    // wrong in the second direction is a crash on the first frame.
    static void __fastcall Detour(void* world, void* a2, void* a3, std::uint32_t a4,
                                  std::uint32_t a5, std::uint32_t a6, std::uint32_t a7,
                                  std::uint32_t a8);
    void PreRender(void* world);
    void PostRender(void* world);
    void LogFrame(bool active);

    // Says once, for each distinct view object that reaches this hook, what it
    // looked like the first time it was seen.
    //
    // The hook does not filter: every call gets the head pose. That is right for
    // the view the player looks through and wrong for anything else the renderer
    // might push through the same function - a reflection probe, a shadow pass,
    // an interface view drawn in world space. Which of those exist can only be
    // read off the running game, and a count of distinct views with their fields
    // of view and clip planes is what reads it.
    void NoteView(std::uintptr_t view);

    // Publishes the frame's pose to the interface hooks, so world-anchored
    // elements are projected through the view the frame was drawn from rather
    // than the one the game would have drawn it from. Called on every frame,
    // including the ones that wrote no tracked view - see UiWorldView::SetFrame.
    void UpdateWorldAnchoredInterface(bool wroteTrackedView);

    // Reads the frame's field of view and the game's un-zoomed one, and works
    // out how much the pose has to shrink for the head to be worth the same
    // distance across the screen. Reports both once, on the first frame the
    // camera reaches, so the basis is in the log with no tracker connected and
    // without loading a save.
    ZoomScale ReadZoomScale(std::uintptr_t view);

    // Whether world-anchored interface elements and the reticle are corrected
    // for the tracked view. Follows the config key of the same name, and is
    // cleared if the interface hooks fail to install - so it means "running",
    // not "asked for".
    bool m_compensateWorldMarkers = false;

    // Which sanity guard refused, and so which latch the report uses.
    enum class RefusalKind { CleanView = 0, TrackedView = 1, Count = 2 };

    // Says once per kind that a guard refused this frame. Refusing silently is
    // the exact symptom a wrong rv_vieworg/rv_viewaxis offset on a new build
    // produces: every frame is dropped, the log still reports the hook as
    // active, and the player reports "no head tracking" with nothing to triage.
    void ReportRefusal(RefusalKind kind, const char* what, const idtech::Vec3& origin,
                       const idtech::Mat3& axis);

    const builds::BuildProfile* m_profile = nullptr;
    HeadTrackingMod* m_mod = nullptr;

    // Carried across the original call so PostRender can put the game's view
    // back exactly as it was, byte for byte, rather than recomputing an inverse.
    idtech::Vec3 m_cleanOrigin{};
    idtech::Mat3 m_cleanAxis{};
    idtech::Vec3 m_renderOrigin{};
    idtech::Mat3 m_renderAxis{};
    bool m_wrote = false;
    bool m_refusalLogged[static_cast<int>(RefusalKind::Count)] = {};

    // The frame's clip planes and field of view, read and never written.
    //
    // The field of view is not decoration. The game narrows it to aim a weapon
    // and to raise the camera, and a narrow field magnifies everything in the
    // frame including the head pose, so `m_fovY` against the game's own
    // un-zoomed `g_fov` is what the pose is scaled by before it is applied - see
    // ZoomScale. The two angles are also what identified this struct in the
    // first place, which is why the whole set is logged.
    float m_fovX = 0.0f;
    float m_fovY = 0.0f;
    float m_nearClip = 0.0f;
    float m_farClip = 0.0f;

    // The game's Field of View slider, read live out of the g_fov cvar so that
    // moving it mid-session is picked up without a relaunch.
    float m_baseFovX = 0.0f;
    ZoomScale m_zoom;

    std::uintptr_t m_imageBase = 0;
    bool m_fovBasisLogged = false;
    bool m_fovUnreadableLogged = false;

    // The last factor a line was written for, and when. Starting at 1 keeps an
    // ordinary un-zoomed session silent and still reports the first zoom.
    float m_reportedFactor = 1.0f;
    unsigned long long m_factorReportedAt = 0;

    // Render-thread only. Bounded so a renderer that hands out a fresh view
    // object per frame cannot turn this into an unbounded log.
    static constexpr int kMaxNotedViews = 16;
    std::uintptr_t m_notedViews[kMaxNotedViews] = {};
    int m_notedViewCount = 0;
    bool m_noteLimitReported = false;

    UiWorldView m_uiWorldView;
};

}  // namespace gc_ht
