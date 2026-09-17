// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "hud_view.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#include "cameraunlock/hooks/hook_manager.h"
#include "headtracking_mod.h"
#include "hud_math.h"
#include "log_throttle.h"
#include "logging.h"
#include "zoom_scale.h"

namespace gc_ht {

namespace {

// One line per interval per path. Long enough that a session's log stays
// readable, short enough that a report about a later moment - "it drifted after
// a while", "it went wrong when I aimed" - has samples covering it.
constexpr ULONGLONG kDiagnosticIntervalMs = 5000;

using MarkerViewFn = std::uint64_t(__fastcall*)(void*, const void*);
using UiFn = void(__fastcall*)(void*, void*);
using DestroyFn = void(__fastcall*)(void*);
using SetPositionFn = void(__fastcall*)(void*, float, float);

MarkerViewFn g_markerView = nullptr;
UiFn g_crosshairUpdate = nullptr;
UiFn g_spriteDraw = nullptr;
DestroyFn g_crosshairDestroy = nullptr;
SetPositionFn g_setPosition = nullptr;

// Written during Start, before the detours are enabled, and read by them
// afterwards. The release store on g_enabled is what publishes both.
const builds::BuildProfile* g_profile = nullptr;
std::atomic<bool> g_enabled{false};

// The reticle element, captured from the crosshair controller's update and
// dropped by its destructor. Atomic because the controller and the draw are not
// guaranteed to run on the same thread.
std::atomic<void*> g_crosshair{nullptr};

// The sprite this mod last moved, so a draw that is no longer compensating can
// put it back exactly once rather than fighting the engine for it every frame.
std::atomic<void*> g_shiftedSprite{nullptr};

// The frame the render hook published, and the lock that stops a reader seeing
// half of one. A lock rather than the atomic triple the rest of the mod passes
// poses through, because this IS a cross-thread handoff: the render thread
// writes it and whichever thread draws the interface reads it, at a moment
// neither of them controls.
struct HudFrame {
    HudPose pose;
    bool active = false;
    ULONGLONG time = 0;
    // Incremented once per published frame, so a latched copy can be told from
    // the live one. A time in milliseconds cannot do this: several frames share
    // one millisecond at any reasonable rate.
    unsigned long long id = 0;
};

SRWLOCK g_frameLock = SRWLOCK_INIT;
HudFrame g_frame;

HudFrame ReadFrame() {
    AcquireSRWLockShared(&g_frameLock);
    const HudFrame frame = g_frame;
    ReleaseSRWLockShared(&g_frameLock);
    return frame;
}

// The contract between the two detours below, and the reason they share a file.
// The marker pass runs first and latches what it drew with; the reticle draw
// that follows reprojects against THAT, and only once it has confirmed it was
// handed the same view object. Thread-local because the pair runs together on
// whichever thread the engine drew on, and two of those at once must not read
// each other's frame.
thread_local HudFrame g_drawFrame;
thread_local const void* g_drawView = nullptr;
thread_local float g_canvasWidth = 0.0f;
thread_local float g_canvasHeight = 0.0f;

template <typename T>
T LoadField(const void* object, std::size_t offset) {
    T value{};
    std::memcpy(&value, static_cast<const unsigned char*>(object) + offset, sizeof(T));
    return value;
}

const idtech::Vec3& ViewOrigin(const unsigned char* view) {
    return *reinterpret_cast<const idtech::Vec3*>(view + g_profile->rv_vieworg_offset);
}

const idtech::Mat3& ViewAxis(const unsigned char* view) {
    return *reinterpret_cast<const idtech::Mat3*>(view + g_profile->rv_viewaxis_offset);
}

float ViewFovX(const unsigned char* view) {
    return *reinterpret_cast<const float*>(view + g_profile->rv_fov_x_offset);
}

float ViewFovY(const unsigned char* view) {
    return *reinterpret_cast<const float*>(view + g_profile->rv_fov_y_offset);
}

// Whether the engine handed over something a head pose can be composed onto.
// The same boundary the camera hook applies to the view it writes: a pose
// composed onto a basis that is not a camera pins the interface to nothing, and
// drawing that frame untouched is the honest answer.
bool IsUsableRenderView(const unsigned char* view) {
    return idtech::IsFinite3(&ViewOrigin(view).x) && idtech::IsOrthonormal(ViewAxis(view)) &&
           IsUsableFov(ViewFovX(view)) && IsUsableFov(ViewFovY(view));
}

void ApplyPoseToView(unsigned char* view, const HudPose& pose) {
    ApplyHudPose(pose, *reinterpret_cast<idtech::Vec3*>(view + g_profile->rv_vieworg_offset),
                 *reinterpret_cast<idtech::Mat3*>(view + g_profile->rv_viewaxis_offset));
}

void LogMarkerPass(const void* marker, const unsigned char* view, bool posed,
                   ULONGLONG frameTime) {
    static TimedLogGate s_gate(kDiagnosticIntervalMs);
    if (!s_gate.ShouldLog()) return;

    // The age of the pose as well as whether one was applied. A marker pass
    // running against a frame published seconds ago is a render hook that has
    // stopped publishing, which reads identically to a tracker that has stopped
    // sending and needs a different fix.
    const ULONGLONG now = GetTickCount64();
    const idtech::Vec3& origin = ViewOrigin(view);
    Log::Line("[hud] markers active=%d age=%llums count=%d canvas=%.0fx%.0f "
              "origin=(%.3f %.3f %.3f)",
              posed, frameTime != 0 ? now - frameTime : 0,
              LoadField<int>(marker, g_profile->hud_marker_count), g_canvasWidth, g_canvasHeight,
              origin.x, origin.y, origin.z);
}

// idLogicUIInworldMarkerElement's camera projection. The engine builds its world
// marker projection out of the view handed in here, so handing it a COPY
// carrying the head pose is what keeps a marker on the thing it is pinned to.
// The engine's own view object is never written to.
std::uint64_t __fastcall MarkerView(void* object, const void* view) {
    if (!g_enabled.load(std::memory_order_acquire)) return g_markerView(object, view);

    g_canvasWidth = LoadField<float>(object, g_profile->hud_marker_canvas_width);
    g_canvasHeight = LoadField<float>(object, g_profile->hud_marker_canvas_height);
    g_drawFrame = ReadFrame();
    g_drawView = view;

    // The copy is what keeps the engine's own view object unwritten, and it is
    // only made on the frames that have a pose to compose onto it - this runs
    // once per marker being placed, so a suppressed frame would otherwise spend
    // a renderView_t memcpy per marker to arrive back at the view it was handed.
    //
    // The guard reads the COPY rather than the original, so the bytes that are
    // checked are the bytes that are projected through.
    alignas(16) unsigned char tracked[builds::kRenderViewSize];
    const void* projectThrough = view;
    if (g_drawFrame.active) {
        std::memcpy(tracked, view, sizeof(tracked));
        // Latched onto the published frame rather than kept in a local: the
        // reticle draw that follows has to know whether this pass actually posed
        // the view, and this is where it reads that from.
        g_drawFrame.active = IsUsableRenderView(tracked);
        if (g_drawFrame.active) {
            ApplyPoseToView(tracked, g_drawFrame.pose);
            projectThrough = tracked;
        }
    }

    const std::uint64_t result = g_markerView(object, projectThrough);
    LogMarkerPass(object, static_cast<const unsigned char*>(view), g_drawFrame.active,
                  g_drawFrame.time);
    return result;
}

// idLogicUISystemController_CrosshairRelic::Update. Nothing else in the process
// names the reticle element, so it is taken from the controller's parent after
// the controller's own update has had its chance to set it.
void __fastcall CrosshairUpdate(void* object, void* context) {
    // The acquire the other two detours take, for the same reason: it is what
    // publishes g_profile, which the next line dereferences. This detour is
    // enabled before Start's release store, so it was the one path in the file
    // reading a published pointer without the load that publishes it.
    if (!g_enabled.load(std::memory_order_acquire)) {
        g_crosshairUpdate(object, context);
        return;
    }
    g_crosshairUpdate(object, context);
    g_crosshair.store(LoadField<void*>(object, g_profile->hud_crosshair_parent),
                      std::memory_order_release);
}

// The same controller's destructor. Dropping the captured element here is what
// keeps the draw below from writing through a pointer the engine has freed, and
// the shifted-sprite latch goes with it because it means nothing once that
// element is gone.
void __fastcall CrosshairDestroy(void* object) {
    // Cleared outright rather than only when the controller's parent still reads
    // back as the captured pointer. A teardown that nulls or reassigns that
    // field before the destructor runs would fail that compare and leave the
    // captured element pointing at memory this call is about to free, and the
    // draw below would then write a visibility byte and read a sprite pointer
    // out of whatever the allocator handed the address to next. Over-clearing
    // costs one frame: the next CrosshairUpdate captures it again.
    //
    // g_shiftedSprite is deliberately NOT cleared with it. It is only ever
    // compared for equality and never dereferenced, so a stale value is safe -
    // and if this controller is not the one whose element is latched, clearing
    // would throw away the way back for an element that is still displaced.
    g_crosshair.store(nullptr, std::memory_order_release);
    g_crosshairDestroy(object);
}

// Where the reticle belongs in the picture this frame was drawn with. False
// hides it: the aim has left the frame, and a reticle clamped to an edge claims
// shots land somewhere they do not.
bool ProjectReticle(const unsigned char* view, const HudPose& pose, float& ndcX, float& ndcY) {
    const idtech::Mat3 cleanAxis = ViewAxis(view);
    idtech::Mat3 trackedAxis = cleanAxis;
    idtech::Vec3 origin = ViewOrigin(view);
    ApplyHudPose(pose, origin, trackedAxis);
    return ProjectHudAim(cleanAxis, trackedAxis, ViewFovX(view), ViewFovY(view), ndcX, ndcY);
}

// Why, as well as where. Printing only the position cannot tell a centred head
// from a reticle that is not being compensated at all: both read ndc=(0,0). Each
// of the three terms behind `compensate` is printed separately because they fail
// for different reasons - `viewmatch` for a draw against some other view or a
// thread that has run no marker pass, `active` for a render hook that published
// a suppressed frame, `posed` for a marker pass that refused the view it was
// handed as not a usable camera, which is what a moved renderView_t offset on a
// future build looks like. The canvas is on the line too, since it is read off
// the marker element and then used to scale into the sprite's space, and the two
// agreeing is an assumption rather than a fact.
void LogReticle(bool visible, bool compensate, bool viewMatch, bool active, bool posed,
                unsigned long long dframe, ULONGLONG dage, float ndcX, float ndcY,
                const HudPoint& at) {
    static TimedLogGate s_gate(kDiagnosticIntervalMs);
    if (!s_gate.ShouldLog()) return;
    Log::Line("[hud] reticle visible=%d compensate=%d viewmatch=%d active=%d posed=%d "
              "dframe=%llu dage=%llums ndc=(%.4f %.4f) pixels=(%.1f %.1f) canvas=%.0fx%.0f",
              visible, compensate, viewMatch, active, posed, dframe, dage, ndcX, ndcY, at.x, at.y,
              g_canvasWidth, g_canvasHeight);
}

// idLogicUISWFSpriteElement::Draw, filtered down to the one element that is the
// reticle. Every other element in the interface draws untouched.
void __fastcall SpriteDraw(void* object, void* context) {
    if (!g_enabled.load(std::memory_order_acquire) ||
        object != g_crosshair.load(std::memory_order_acquire)) {
        g_spriteDraw(object, context);
        return;
    }

    auto* element = static_cast<unsigned char*>(object);
    void* sprite = LoadField<void*>(element, g_profile->hud_sprite_instance);
    const auto* view = LoadField<const unsigned char*>(context, g_profile->hud_draw_render_view);
    const float* layout = reinterpret_cast<const float*>(element + g_profile->hud_layout_rectangle);

    // Only against the view the marker pass posed, and only if it posed it. A
    // draw for some other view has no tracked picture to reproject into, and
    // moving the reticle on the strength of another view's pose would put it
    // where nothing is aiming.
    //
    // Three terms, and deliberately NO staleness cutoff. Nothing clears
    // g_drawFrame at a frame boundary and the view pointer is a member address
    // that recurs, so a draw with no marker pass behind it reused the previous
    // pose indefinitely - and with tracking switched off that left the reticle
    // displaced by whatever pose the last marker pass caught.
    //
    // `live.active` is what closes that, and it closes it exactly: SetFrame
    // publishes on every call of the render hook, so it reads false on the first
    // one after the toggle and the way back below runs.
    //
    // An age cutoff was tried here and taken out again. Refusing a stale frame
    // does not leave the reticle alone for a frame - it falls into the way back
    // and MOVES it to screen centre, so a pose age that crosses the threshold
    // from frame to frame strobes the reticle between the aim point and the
    // middle of the screen. .lab/NOTES.md records a previous session removing an
    // arbitrary pose-age cutoff from this path for that reason, against the
    // running game, and that measurement stands.
    //
    // The two staleness measures are logged instead, so a session can say what
    // the numbers actually are before anything is gated on them. They measure
    // different things: `dframe` counts render-hook publishes between the marker
    // pass and this draw, and goes to zero if the hook stops running at all,
    // which `dage` in milliseconds is what catches.
    const bool viewMatch = view != nullptr && view == g_drawView;
    const HudFrame live = ReadFrame();
    const unsigned long long dframe = live.id - g_drawFrame.id;
    const ULONGLONG dage = g_drawFrame.time != 0 ? live.time - g_drawFrame.time : 0;
    const bool compensate = viewMatch && live.active && g_drawFrame.active;
    float ndcX = 0.0f;
    float ndcY = 0.0f;
    const bool visible = !compensate || ProjectReticle(view, g_drawFrame.pose, ndcX, ndcY);

    // Placed BEFORE the engine's draw, because the draw is what consumes the
    // position - the way back below is the proof it persists across draws rather
    // than being rebuilt by each one. Setting it afterwards meant every frame
    // was drawn at the previous frame's offset. A held pose cannot show that,
    // which is why every screenshot behind this path missed it; a moving head
    // would show the reticle trailing the aim.
    //
    // The sprite moves through the engine's own position setter, which preserves
    // the reticle's native appearance and spread. The second half of the last
    // condition is the way back: a draw that is no longer compensating still
    // places the sprite once, at an offset of zero, which returns it to its
    // layout position and clears the latch. That branch deliberately does not
    // require a canvas - at an offset of zero the placement is the layout
    // position whatever the canvas is, and requiring one meant a reticle latched
    // on a thread that never ran the marker pass could never be put back.
    const HudPoint at =
        PlaceOnCanvas(layout[0], layout[1], ndcX, ndcY, g_canvasWidth, g_canvasHeight);
    // The way back is gated on this thread having run a marker pass at all.
    // Without that, a draw arriving on a thread that never runs one takes the
    // restore branch on every frame, so a compensating draw offsets the reticle
    // and the non-matching draw immediately puts it back - the reticle strobes
    // between the aim point and screen centre. The canvas is not required here,
    // because at an offset of zero the placement is the layout position whatever
    // the canvas is.
    const bool placeable = compensate ? (g_canvasWidth > 0.0f && g_canvasHeight > 0.0f)
                                      : (g_drawView != nullptr && g_shiftedSprite.load() == sprite);

    if (sprite != nullptr && visible && placeable) {
        g_setPosition(sprite, at.x, at.y);
        g_shiftedSprite.store(compensate ? sprite : nullptr);
    }

    // Hidden by suppressing the element's own visibility for the length of the
    // engine's draw and putting it straight back, so nothing outside this call
    // sees the flag changed. __finally rather than a scope guard: the engine's
    // draw can unwind, and a C++ destructor does not run for an SEH unwind -
    // which would leave the reticle switched off for the rest of the session.
    const bool wasVisible = *reinterpret_cast<bool*>(element + g_profile->hud_element_visible);
    *reinterpret_cast<bool*>(element + g_profile->hud_element_visible) = wasVisible && visible;
    __try {
        g_spriteDraw(object, context);
    } __finally {
        *reinterpret_cast<bool*>(element + g_profile->hud_element_visible) = wasVisible;
    }

    // Written whether or not the sprite moved. A draw that reached here and
    // placed nothing is a different fault from one that never reached here, and
    // a line only on the moving path cannot tell them apart.
    LogReticle(wasVisible && visible, compensate, viewMatch, live.active, g_drawFrame.active,
               dframe, dage, ndcX, ndcY, at);
}

}  // namespace

bool UiWorldView::Start(const builds::BuildProfile& profile) {
    g_profile = &profile;

    auto& hooks = cameraunlock::hooks::HookManager::Instance();
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    g_setPosition = reinterpret_cast<SetPositionFn>(base + profile.hud_sprite_position_rva);

    struct Detour {
        std::uint32_t rva;
        void* replacement;
        void** original;
    };
    const Detour detours[] = {
        {profile.hud_marker_view_rva, reinterpret_cast<void*>(&MarkerView),
         reinterpret_cast<void**>(&g_markerView)},
        {profile.hud_crosshair_update_rva, reinterpret_cast<void*>(&CrosshairUpdate),
         reinterpret_cast<void**>(&g_crosshairUpdate)},
        {profile.hud_crosshair_destroy_rva, reinterpret_cast<void*>(&CrosshairDestroy),
         reinterpret_cast<void**>(&g_crosshairDestroy)},
        {profile.hud_sprite_draw_rva, reinterpret_cast<void*>(&SpriteDraw),
         reinterpret_cast<void**>(&g_spriteDraw)},
    };

    std::size_t installed = 0;
    for (const Detour& detour : detours) {
        void* target = reinterpret_cast<void*>(base + detour.rva);
        auto status = hooks.CreateHook(target, detour.replacement, detour.original);
        if (status == cameraunlock::hooks::HookStatus::Ok) status = hooks.EnableHook(target);
        if (status != cameraunlock::hooks::HookStatus::Ok) {
            Log::Line("[hud] hook +0x%X failed: %s; world markers and the reticle are drawn as "
                      "the game draws them",
                      detour.rva, cameraunlock::hooks::HookStatusToString(status));
            // The ones already in come back out. The camera hook installs these
            // last so that every refusal it reports can say the game is
            // unmodified, and detours left behind by a partial install make that
            // line untrue for the rest of the session - this mod's code stays in
            // the engine's marker and reticle paths on a build it then declined
            // to touch. MH_RemoveHook disables an enabled hook on the way out,
            // so one call per entry is the whole unwind.
            for (std::size_t i = 0; i < installed; ++i) {
                hooks.RemoveHook(reinterpret_cast<void*>(base + detours[i].rva));
            }
            return false;
        }
        ++installed;
    }

    // Last, and a release store: it is what publishes g_profile and every
    // trampoline above to the threads that are about to read them.
    g_enabled.store(true, std::memory_order_release);
    Log::Line("[hud] marker projection and crosshair hooks installed");
    return true;
}

void UiWorldView::SetFrame(const HeadTrackingMod& mod, bool active) {
    HudFrame frame;
    frame.time = GetTickCount64();
    frame.active = active;
    if (active) {
        mod.GetRotationRadians(frame.pose.yaw, frame.pose.pitch, frame.pose.roll);
        mod.GetPositionOffset(frame.pose.forward, frame.pose.left, frame.pose.up);
        frame.pose.worldYaw = mod.IsWorldSpaceYaw();
    }
    AcquireSRWLockExclusive(&g_frameLock);
    frame.id = g_frame.id + 1;
    g_frame = frame;
    ReleaseSRWLockExclusive(&g_frameLock);
}

}  // namespace gc_ht
