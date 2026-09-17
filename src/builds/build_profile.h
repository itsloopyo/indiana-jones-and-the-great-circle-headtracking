// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include <cstdint>

#include "cameraunlock/memory/pe_fingerprint.h"

namespace gc_ht::builds {

// sizeof(renderView_t), from the engine's own field reflection: idViewComponent
// is a renderView_t at offset 0 followed by its transform handle at +0x9A0.
//
// Build-independent, so it sits beside the profile rather than inside it, and
// shared rather than restated: the camera hook reads one renderView_t past a
// view to sanity-check what it is looking at, and the interface hooks copy a
// whole one to project a marker through, and the two sizes have to agree.
inline constexpr std::uint32_t kRenderViewSize = 0x9A0u;

// Everything this mod needs to know about one shipped build of
// TheGreatCircle.exe. Routed by PE fingerprint, never by a version string: the
// mod has to identify the build from inside the running process, and the PE
// header is the only thing there that says which binary this is. The packaged
// executable cannot even be opened from disk on a Game Pass install, so a
// file-based check was never an option.
//
// Profiles are APPEND-ONLY. A patch that moves an RVA gets a NEW profile added
// to the top of kKnownProfiles; the existing one stays exactly as it is, so a
// player who has not taken the patch keeps matching it. Never edit an RVA in
// place - that strands every user on the older build with no fix available to
// them at all.
struct BuildProfile {
    const char* name;
    cameraunlock::memory::PeFingerprint fingerprint;

    // idRenderWorldLocal::Render. Takes the render world in rcx and reads the
    // frame's renderView_t out of the world at `render_view_offset`; the FOV
    // sanity check, the projection matrix and every culling decision in the
    // frame are derived from it after this function is entered. Writing the
    // view at the top of the detour and putting it back at the bottom is
    // therefore the whole render-phase injection: the frustum turns with the
    // view, so nothing is culled at the edges of a turned frame, and no game
    // code outside this call ever observes the tracked pose.
    std::uint32_t render_rva;

    // Byte offset of the frame's renderView_t inside idRenderWorldLocal.
    std::uint32_t render_view_offset;

    // Byte offsets inside renderView_t.
    //
    // Derived two ways that agree. From the code: the function above reads
    // `fov_x` and `fov_y` as the pair it rejects the frame over, and a float at
    // +0x84. From a live process: a sweep for an orthonormal basis with two
    // angles nearby whose half-angle tangents sit in the window's aspect found
    // a block reading 90.0 / 58.7155 (exactly 16:9), 0.06 and 8192.0 for the
    // clip planes, a position in metres, and the basis. The spacing between
    // those fields is the same in both, which is what fixes the numbers below.
    std::uint32_t rv_fov_x_offset;     // full horizontal angle, degrees
    std::uint32_t rv_fov_y_offset;     // full vertical angle, degrees
    std::uint32_t rv_near_clip_offset;
    std::uint32_t rv_far_clip_offset;
    std::uint32_t rv_vieworg_offset;   // idVec3, world position of the eye, metres
    std::uint32_t rv_viewaxis_offset;  // idMat3, rows are forward / left / up

    // The live value of the `g_fov` cvar, as a float in .data. This is the
    // game's own Field of View slider - the cvar's description is "camera field
    // of view" and the video settings clamp it with ui_settings_video_fovMin /
    // fovMax - and it is the angle `fov_x` above equals whenever nothing is
    // zoomed. Dividing one by the other is how far the game has zoomed in, and
    // it is the engine's own arithmetic: the help text for the aim sensitivity
    // cvar describes the zoom as `curFov/g_fov`.
    //
    // Read every frame rather than once, so moving the slider mid-session is
    // picked up without a relaunch.
    std::uint32_t g_fov_rva;

    // idRenderWorldLocal's vtable, in .rdata, from the image's MSVC RTTI. The
    // world is heap allocated and nothing in the image points at it, so this is
    // what the runtime sweep for it keys on.
    std::uint32_t render_world_vtable_rva;

    // The interface functions the world-marker and reticle compensation hooks
    // sit on. Each is the engine's own symbol, so a new build re-derives them by
    // name rather than by matching bytes.
    //
    // DECLARATION ORDER IS LOAD-BEARING. Every profile is an aggregate
    // initialiser listing these positionally, so swapping two members here
    // silently swaps two RVAs in every <store>_offsets.cpp and hooks the wrong
    // functions. Append, never reorder.

    // idLogicUIInworldMarkerElement's camera projection, which takes the marker
    // element and the render view a world anchor is to be projected through.
    // Handing it a view carrying the head pose is what keeps a marker on the
    // thing it is pinned to while the head turns.
    std::uint32_t hud_marker_view_rva;

    // idLogicUISystemController_CrosshairRelic::Update. How the reticle element
    // is identified at all: the controller holds it as its parent.
    std::uint32_t hud_crosshair_update_rva;

    // idLogicUISWFSpriteElement::Draw, the per-element draw the reticle passes
    // through, and idSWFSpriteInstance_Lite's global position setter, which is
    // what actually moves it on the canvas.
    std::uint32_t hud_sprite_draw_rva;
    std::uint32_t hud_sprite_position_rva;

    // idLogicUISystemController_CrosshairRelic's destructor, which drops the
    // identification above when the controller goes away, so a stale element is
    // never written to.
    std::uint32_t hud_crosshair_destroy_rva;

    // Field offsets inside those same interface objects. They live here for the
    // same reason the renderView_t offsets above do: a struct member is exactly
    // as build-pinned as an RVA, and the fingerprint has to route every number
    // the hooks use or it is only routing most of them.
    //
    // A build that moves one cannot fingerprint as matched - Matches compares
    // the whole triple - so the cost is not paid today. It is paid the day a
    // SECOND profile is appended for a patched build: file-scope constants in
    // hud_view.cpp would silently stay at the first build's values while every
    // RVA beside them routed correctly, which is the one failure the registry
    // exists to make impossible.

    // idLogicUIInworldMarkerElement: the marker count (logged, so a pass
    // carrying none of them is distinguishable from a hook that never ran) and
    // the canvas the elements are laid out on.
    std::uint32_t hud_marker_count;
    std::uint32_t hud_marker_canvas_width;
    std::uint32_t hud_marker_canvas_height;

    // idLogicUISystemController_CrosshairRelic's parent element: the reticle
    // itself, which nothing else in the process names.
    std::uint32_t hud_crosshair_parent;

    // idLogicUISWFSpriteElement: the sprite instance the position setter moves,
    // the baseline layout rectangle (its x and y are the first two floats), and
    // the visibility flag the draw honours.
    std::uint32_t hud_sprite_instance;
    std::uint32_t hud_layout_rectangle;
    std::uint32_t hud_element_visible;

    // The renderView_t the draw context was built for, which is what ties a
    // reticle draw back to the marker pass that published the pose for it.
    std::uint32_t hud_draw_render_view;
};

}  // namespace gc_ht::builds
