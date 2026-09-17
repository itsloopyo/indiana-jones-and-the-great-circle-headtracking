// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "camera_hook.h"

#include <windows.h>

#include <atomic>
#include <cmath>

#include "angle_units.h"
#include "cameraunlock/hooks/hook_manager.h"
#include "headtracking_mod.h"
#include "log_throttle.h"
#include "logging.h"

namespace gc_ht {

namespace {

using RenderFn = void(__fastcall*)(void*, void*, void*, std::uint32_t, std::uint32_t,
                                   std::uint32_t, std::uint32_t, std::uint32_t);

// Atomic, and read on the detour's null branch as well as its live one. MinHook
// writes it inside CreateHook and its EnableHook barrier does order that write
// ahead of the first detour call, but a relaxed load costs nothing on x64 and
// removes the need to rest a hot-path read on that argument.
std::atomic<RenderFn> g_original{nullptr};
// Atomic because the detour is live from the moment EnableHook returns, so the
// game's render thread reads this while the bootstrap thread is still writing
// it. The release store in Arm() is also what publishes m_profile and m_mod,
// both written earlier on the bootstrap thread and dereferenced on the first
// frame that sees a non-null pointer here.
std::atomic<CameraHook*> g_hook{nullptr};

// Dense at first, because that is where a wrong offset shows, then a thin
// trickle for the rest of the session, which is what a late report ("it drifted
// after an hour", "it stopped when I loaded a save") is read from.
constexpr unsigned kBurstLines = 4;
constexpr unsigned kEarlyLines = 20;
constexpr unsigned kEarlyIntervalFrames = 600;
constexpr unsigned kSteadyIntervalFrames = 2000;

}  // namespace

bool CameraHook::Install(const builds::BuildProfile& profile, HeadTrackingMod& mod) {
    m_profile = &profile;
    m_mod = &mod;
    m_compensateWorldMarkers = mod.GetConfig().compensate_world_markers;

    auto& hooks = cameraunlock::hooks::HookManager::Instance();
    const auto initStatus = hooks.Initialize();
    if (initStatus != cameraunlock::hooks::HookStatus::Ok) {
        Log::Line("[camera] MinHook init failed (%s); the mod is dormant and the game is "
                  "unmodified", cameraunlock::hooks::HookStatusToString(initStatus));
        return false;
    }

    m_imageBase = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto base = m_imageBase;
    void* target = reinterpret_cast<void*>(base + profile.render_rva);

    RenderFn original = nullptr;
    const auto createStatus =
        hooks.CreateHook(target, reinterpret_cast<void*>(&CameraHook::Detour),
                         reinterpret_cast<void**>(&original));
    g_original.store(original, std::memory_order_release);
    if (createStatus != cameraunlock::hooks::HookStatus::Ok) {
        Log::Line("[camera] could not hook idRenderWorldLocal::Render at +0x%X (%s); the mod "
                  "is dormant and the game is unmodified", profile.render_rva,
                  cameraunlock::hooks::HookStatusToString(createStatus));
        return false;
    }

    const auto enableStatus = hooks.EnableHook(target);
    if (enableStatus != cameraunlock::hooks::HookStatus::Ok) {
        Log::Line("[camera] could not enable the idRenderWorldLocal::Render hook (%s); the mod "
                  "is dormant and the game is unmodified",
                  cameraunlock::hooks::HookStatusToString(enableStatus));
        return false;
    }

    Log::Line("[camera] hooked idRenderWorldLocal::Render at +0x%X, render view at +0x%X",
              profile.render_rva, profile.render_view_offset);

    // Last, and only once the camera is in. Every refusal above says the game is
    // unmodified, and four interface detours installed ahead of them would make
    // that line untrue: the mod would have left its own code in the engine's
    // marker and reticle paths on a build it then declined to touch.
    if (m_compensateWorldMarkers) m_compensateWorldMarkers = m_uiWorldView.Start(profile);
    return true;
}

void CameraHook::Arm() {
    // Deliberately not the tail of Install. Every failure in Install leaves the
    // caller free to destroy this object, and a global still pointing at a
    // destroyed hook is one enabled detour away from a use-after-free - so the
    // store cannot happen until the caller has decided to keep it. A frame
    // landing before this store takes the null branch in Detour, which is the
    // plain pass-through to the game's own Render.
    g_hook.store(this, std::memory_order_release);
}

void __fastcall CameraHook::Detour(void* world, void* a2, void* a3, std::uint32_t a4,
                                   std::uint32_t a5, std::uint32_t a6, std::uint32_t a7,
                                   std::uint32_t a8) {
    const RenderFn original = g_original.load(std::memory_order_acquire);
    CameraHook* const hook = g_hook.load(std::memory_order_acquire);
    if (hook == nullptr || world == nullptr) {
        original(world, a2, a3, a4, a5, a6, a7, a8);
        return;
    }
    // __finally rather than a call placed after the trampoline: if the game's
    // own Render unwinds, the tracked view is left sitting in the render world,
    // and the next frame's PreRender snapshots THAT as its clean view and
    // composes on top of it, so the pose compounds frame after frame until the
    // view is unusable. The restore has to run on every exit, and a C++
    // destructor does not run for an SEH unwind.
    //
    // PreRender is INSIDE the guard for the same reason. It writes the tracked
    // view and then goes on to log and to publish the interface frame, so an
    // unwind out of that tail would leave the engine holding a tracked basis
    // with no restore scheduled - the exact compounding this guard exists to
    // stop. PostRender gates on m_wrote, so an unwind before the write costs
    // nothing.
    __try {
        hook->PreRender(world);
        original(world, a2, a3, a4, a5, a6, a7, a8);
    } __finally {
        hook->PostRender(world);
    }
}

void CameraHook::NoteView(std::uintptr_t view) {
    for (int i = 0; i < m_notedViewCount; ++i) {
        if (m_notedViews[i] == view) return;
    }
    if (m_notedViewCount >= kMaxNotedViews) {
        if (m_noteLimitReported) return;
        m_noteLimitReported = true;
        Log::Line("[camera] more than %d distinct view objects have reached the hook; no "
                  "longer listing them", kMaxNotedViews);
        return;
    }
    m_notedViews[m_notedViewCount++] = view;
    const builds::BuildProfile& p = *m_profile;
    // The view's own fields and nothing else. There was a "trailing16" term here
    // reading the sixteen floats one renderView_t past the view and calling them
    // idView::worldSpaceMVPMatrix - but the view this hook is handed is
    // `world + render_view_offset` with that offset 0, so it is the head of
    // idRenderWorldLocal and not an idView at all. What it printed was whatever
    // that object holds at +0x9A0, in a format that made it look like evidence.
    // .lab/NOTES.md records three false trails of exactly that shape, each of
    // which cost a run.
    Log::Line("[camera] view #%d at %p fov=%.2fx%.2f clip=%.3f..%.1f",
              m_notedViewCount, reinterpret_cast<void*>(view),
              *reinterpret_cast<const float*>(view + p.rv_fov_x_offset),
              *reinterpret_cast<const float*>(view + p.rv_fov_y_offset),
              *reinterpret_cast<const float*>(view + p.rv_near_clip_offset),
              *reinterpret_cast<const float*>(view + p.rv_far_clip_offset));
}

ZoomScale CameraHook::ReadZoomScale(std::uintptr_t view) {
    const builds::BuildProfile& p = *m_profile;
    m_fovX = *reinterpret_cast<const float*>(view + p.rv_fov_x_offset);
    m_fovY = *reinterpret_cast<const float*>(view + p.rv_fov_y_offset);
    m_baseFovX = *reinterpret_cast<const float*>(m_imageBase + p.g_fov_rva);

    // fov_y, not fov_x. The game renders a 16:9 viewport into whatever shape the
    // window is, and derives fov_x from the vertical angle and that viewport's
    // rounded aspect - so fov_x is the derived one and only fov_y carries the
    // game's own field of view. See kEngineReferenceAspect for the measurement.
    const ZoomScale zoom = ComputeZoomScale(m_fovY, m_baseFovX);

    // Once, on the first frame the camera reaches, whether or not a pose has
    // arrived. A factor that is wrong by a constant reads exactly like a factor
    // that is right, so every term of it has to be on a line a human can check,
    // and the check is simply that ordinary gameplay reads 1.0000.
    if (!m_fovBasisLogged) {
        m_fovBasisLogged = true;
        const float aspect = IsUsableFov(m_fovY)
                                 ? std::tan(m_fovX * kHalfDegreesToRadians) /
                                       std::tan(m_fovY * kHalfDegreesToRadians)
                                 : 0.0f;
        Log::Line("[camera] field of view: the frame is drawn at %.4f wide by %.4f high in "
                  "degrees (viewport aspect %.5f), and the g_fov slider reads %.4f horizontal, "
                  "which is %.4f vertical at the engine's 16:9 reference; zoom factor %.4f, and "
                  "ordinary play must read 1.0000",
                  m_fovX, m_fovY, aspect, m_baseFovX,
                  SliderFovAsVerticalDegrees(m_baseFovX), zoom.factor);
    }

    if (!zoom.known) {
        if (!m_fovUnreadableLogged) {
            m_fovUnreadableLogged = true;
            Log::Line("[camera] the field of view read back as %.4f high against %.4f from "
                      "g_fov, which is not a projection; the head pose is applied unscaled and "
                      "will feel stronger while the game is zoomed in", m_fovY, m_baseFovX);
        }
        return zoom;
    }

    // Every change the game makes to its field of view, rate limited rather than
    // counted. A line that fires once answers the first zoom of a session and is
    // silent for every one after, and "the tracking is wrong while I am aiming"
    // is a report about a later one.
    if (std::fabs(zoom.factor - m_reportedFactor) > 0.02f) {
        const ULONGLONG now = GetTickCount64();
        if (now - m_factorReportedAt >= 1000) {
            m_factorReportedAt = now;
            m_reportedFactor = zoom.factor;
            Log::Line("[camera] the frame is now drawn at %.4f high against %.4f vertical from "
                      "g_fov, so the head pose is scaled by %.4f to move the picture by as much "
                      "as it does unzoomed",
                      m_fovY, SliderFovAsVerticalDegrees(m_baseFovX), zoom.factor);
        }
    }
    return zoom;
}

void CameraHook::PreRender(void* world) {
    const builds::BuildProfile& p = *m_profile;
    const auto view = reinterpret_cast<std::uintptr_t>(world) + p.render_view_offset;
    NoteView(view);

    auto* org = reinterpret_cast<idtech::Vec3*>(view + p.rv_vieworg_offset);
    auto* axis = reinterpret_cast<idtech::Mat3*>(view + p.rv_viewaxis_offset);

    m_nearClip = *reinterpret_cast<const float*>(view + p.rv_near_clip_offset);
    m_farClip = *reinterpret_cast<const float*>(view + p.rv_far_clip_offset);
    m_zoom = ReadZoomScale(view);

    m_cleanOrigin = *org;
    m_cleanAxis = *axis;
    m_renderOrigin = m_cleanOrigin;
    m_renderAxis = m_cleanAxis;
    m_wrote = false;

    const bool active = m_mod->UpdateForFrame(m_zoom);
    const bool cleanUsable =
        idtech::IsOrthonormal(m_cleanAxis) && idtech::IsFinite3(&m_cleanOrigin.x);
    // Only while the mod is actually trying to inject. Reporting on an inactive
    // frame would spend the single diagnostic on a frame nothing was going to be
    // written to anyway.
    if (active && !cleanUsable) {
        ReportRefusal(RefusalKind::CleanView,
                      "the view read out of the frame is not a usable camera", m_cleanOrigin,
                      m_cleanAxis);
    }
    if (!active || !cleanUsable) {
        LogFrame(active);
        UpdateWorldAnchoredInterface(false);
        return;
    }

    idtech::Vec3 renderOrigin = m_cleanOrigin;
    idtech::Mat3 renderAxis = m_cleanAxis;
    if (m_mod->BuildTrackedView(renderOrigin, renderAxis)) {
        if (idtech::IsOrthonormal(renderAxis) && idtech::IsFinite3(&renderOrigin.x)) {
            *org = renderOrigin;
            *axis = renderAxis;
            m_renderOrigin = renderOrigin;
            m_renderAxis = renderAxis;
            m_wrote = true;
        } else {
            ReportRefusal(RefusalKind::TrackedView, "the tracked view came out malformed",
                          renderOrigin, renderAxis);
        }
    }
    LogFrame(active);
    UpdateWorldAnchoredInterface(m_wrote);
}

void CameraHook::ReportRefusal(RefusalKind kind, const char* what, const idtech::Vec3& origin,
                               const idtech::Mat3& axis) {
    // A latch per reason, not one for both. They are different faults with
    // different next steps, and a single latch means whichever happens first
    // permanently silences the other.
    bool& logged = m_refusalLogged[static_cast<int>(kind)];
    if (logged) return;
    logged = true;
    const builds::BuildProfile& p = *m_profile;
    // The OFFENDING pair, passed in. This used to print m_cleanOrigin/m_cleanAxis
    // for both kinds, which for a malformed TRACKED view meant printing the one
    // pair that had just been validated - a line asserting a fault and showing
    // data that cannot be it, once per session, with no way to ask again.
    Log::Line("[camera] refusing to inject: %s. org=(%g %g %g) fwd=(%g %g %g) read at "
              "+0x%X/+0x%X. Head tracking will not appear while this holds.",
              what, origin.x, origin.y, origin.z, axis.m[0], axis.m[1], axis.m[2],
              p.rv_vieworg_offset, p.rv_viewaxis_offset);
}

void CameraHook::PostRender(void* world) {
    if (!m_wrote) return;
    const builds::BuildProfile& p = *m_profile;
    const auto view = reinterpret_cast<std::uintptr_t>(world) + p.render_view_offset;

    // The game's own view goes straight back. Nothing in game logic reads this
    // copy, so this is not what decouples aim - the write only ever existed
    // between here and the Render call, and the projection, the frustum and
    // every culling decision the frame is drawn with were derived from it inside
    // that window. What the restore buys is that a frame the engine renders
    // WITHOUT refilling the view from the game - a repeated present, a frame
    // during a stall - starts from the game's view again instead of compounding
    // the head rotation frame after frame.
    *reinterpret_cast<idtech::Vec3*>(view + p.rv_vieworg_offset) = m_cleanOrigin;
    *reinterpret_cast<idtech::Mat3*>(view + p.rv_viewaxis_offset) = m_cleanAxis;
    m_wrote = false;
}

void CameraHook::UpdateWorldAnchoredInterface(bool wroteTrackedView) {
    if (!m_compensateWorldMarkers) return;
    m_uiWorldView.SetFrame(*m_mod, wroteTrackedView);
}

void CameraHook::LogFrame(bool active) {
    static LogThrottle s_throttle(kBurstLines, kEarlyLines, kEarlyIntervalFrames,
                                  kSteadyIntervalFrames);
    if (!s_throttle.ShouldLog()) return;

    // The pose that was actually applied, on the same line as the basis it was
    // applied to. Without it a line showing an unchanged view cannot be told
    // apart three ways - no packet arrived, a packet arrived and was zero, or
    // the pose arrived and the rotation threw it away - and those need different
    // fixes. Printing it is the difference between reading the answer and
    // guessing at it.
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    const bool haveRotation = m_mod->GetRotationRadians(yaw, pitch, roll);
    float fwd = 0.0f, left = 0.0f, up = 0.0f;
    const bool havePosition = m_mod->GetPositionOffset(fwd, left, up);
    // The whole basis, not just the forward row. Which row is forward, which is
    // left and which is up is the one thing about this engine's idMat3 that was
    // carried over from the ancestor rather than measured here, so the line has
    // to carry enough for a reader to settle it against what is on screen.
    Log::Line("[camera] %s org=(%.3f %.3f %.3f) fwd=(%.4f %.4f %.4f) left=(%.4f %.4f %.4f) "
              "up=(%.4f %.4f %.4f) -> org=(%.3f %.3f %.3f) fwd=(%.4f %.4f %.4f) "
              "fov=%.2fx%.2f clip=%.3f..%.1f zoom=%.4f rot[%s]=(%.2f %.2f %.2f) "
              "pos[%s]=(%.3f %.3f %.3f)",
              active ? "active" : "idle",
              m_cleanOrigin.x, m_cleanOrigin.y, m_cleanOrigin.z,
              m_cleanAxis.m[0], m_cleanAxis.m[1], m_cleanAxis.m[2],
              m_cleanAxis.m[3], m_cleanAxis.m[4], m_cleanAxis.m[5],
              m_cleanAxis.m[6], m_cleanAxis.m[7], m_cleanAxis.m[8],
              m_renderOrigin.x, m_renderOrigin.y, m_renderOrigin.z,
              m_renderAxis.m[0], m_renderAxis.m[1], m_renderAxis.m[2],
              m_fovX, m_fovY, m_nearClip, m_farClip, m_zoom.factor,
              haveRotation ? "yes" : "no", yaw * kRadiansToDegrees,
              pitch * kRadiansToDegrees, roll * kRadiansToDegrees,
              havePosition ? "yes" : "no", fwd, left, up);
}

}  // namespace gc_ht
