// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "builds/build_profile.h"

namespace gc_ht::builds {

// Xbox Game Pass / Microsoft Store build of Indiana Jones and the Great Circle,
// package BethesdaSoftworks.ProjectRelic 1.16.1.0, TheGreatCircle.exe.
//
// Every number below was read off the image as it is mapped in the running
// process: relative virtual addresses, struct field offsets and a PE header
// fingerprint, and nothing else.
//
// render_rva is the function carrying "idRenderWorldLocal::Render: bad FOVs:
// %f, %f" and "idRenderView: Projection Matrix Invert failed!". Both references
// sit in cold fragments that .pdata chains back to it.
//
// render_view_offset is 0 because the first thing the function does is copy a
// renderView_t from the START of its first argument into a working copy at
// +0x18FC0, and everything after that - the FOV check, the projection, the
// frustum - reads the copy. A write to +0x18FC0 at the top of the detour is
// overwritten a few instructions later and never reaches the frame; a write at
// offset 0 is what the engine's own copy carries forward, and a screenshot is
// what confirms the rendered frame turns.
//
// The renderView_t offsets came from two sources that agree, which is what makes
// them worth shipping. From the code: the FOV pair at +0x64/+0x68 and a float
// read at +0x84. From a live process: a sweep for an orthonormal basis with two
// angles nearby whose half-angle tangents sit in the ratio of the window being
// rendered into found a block reading 90.0000 and 58.7155 - exactly 16:9 - with
// 0.06 and 8192.0 for the clip planes, a position in metres and the basis. The
// spacing between those fields in the live block is the same as the spacing
// between the offsets the code uses, so the two pin each other.
//
// g_fov_rva is the live float of the `g_fov` cvar. Every cvar in this image is
// a record in .data whose name, default-value and description pointers sit at
// +0x20, +0x28 and +0x30, and whose live int and float values sit at the very
// start, 0x20 bytes ahead of the name pointer. Four cvars settle that layout
// because their live values differ from their compiled defaults and match this
// machine's settings: r_renderWidth read 2560 against a default of "0",
// r_fullscreen read 2, com_adaptiveTickMaxHz read 120 against a default of
// "1000", and g_fov read 90. The record's name pointer is at +0x03E9B6C0, so
// the float is at +0x03E9B6A4.
//
// render_world_vtable_rva is idRenderWorldLocal's, from the image's MSVC RTTI:
// type descriptor .?AVidRenderWorldLocal@@ to its complete object locator, to
// the .rdata slot holding that locator, whose next qword is the vtable. It is
// recorded for diagnostics; the render hook reaches the world through the
// hooked function's own argument rather than by sweeping for it.
extern const BuildProfile kGdkProfile_20260527 = {
    "gdk-win64-20260527",
    { 0x6A16BE24u, 0x07456000u, 0x04B853D2u },

    0x00CAF760u,  // idRenderWorldLocal::Render
    0x00000000u,  // renderView_t sits at the start of Render's first argument

    0x64u,        // renderView_t::fov_x     full horizontal angle, degrees
    0x68u,        // renderView_t::fov_y     full vertical angle, degrees
    0x84u,        // renderView_t::nearClip  metres
    0x88u,        // renderView_t::farClip   metres
    0xA8u,        // renderView_t::vieworg   idVec3, metres
    0xB4u,        // renderView_t::viewaxis  idMat3, rows forward / left / up

    0x03E9B6A4u,  // g_fov, live float value

    0x03631098u,  // idRenderWorldLocal vtable
    0x01BD4B90u,  // idLogicUIInworldMarkerElement camera projection
    0x01C621F0u,  // idLogicUISystemController_CrosshairRelic update
    0x01C08400u,  // idLogicUISWFSpriteElement draw
    0x00701360u,  // idSWFSpriteInstance_Lite global position setter
    0x01C616D0u,  // idLogicUISystemController_CrosshairRelic destructor

    // The interface field offsets, read off the running process the same way the
    // renderView_t ones above were: the marker count and canvas out of
    // idLogicUIInworldMarkerElement at the marker camera setup, the parent
    // element out of idLogicUISystemController_CrosshairRelic at its update, and
    // the sprite instance, layout rectangle and visibility flag out of
    // idLogicUISWFSpriteElement at its draw. The canvas read 3840x2160 with the
    // game rendering at that size and the element transforms identity.
    0x358u,       // idLogicUIInworldMarkerElement::markerCount
    0x374u,       // idLogicUIInworldMarkerElement canvas width
    0x378u,       // idLogicUIInworldMarkerElement canvas height
    0x60u,        // idLogicUISystemController_CrosshairRelic parent element
    0x110u,       // idLogicUISWFSpriteElement sprite instance
    0x9Cu,        // idLogicUISWFSpriteElement baseline layout rectangle
    0xACu,        // idLogicUISWFSpriteElement visibility flag
    0xE0u,        // draw context renderView_t
};

}  // namespace gc_ht::builds
