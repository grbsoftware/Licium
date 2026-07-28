// licium_cockpit.h — the Tune cockpit's docked tool strip.
//
// A column of tool buttons welded to the right edge of the canvas. Within the
// X browser it is PERMANENT (Gary 2026-07-26: "like adobe but not floating or
// dismissible") - no toggle, no close, it is chrome rather than a panel. It is
// drawn ONLY while the browser is open, though (Gary 2026-07-27: "that tools
// should only show up in X"), which follows from what the tools are: every one
// acts on the SELECTED ROLL, and outside the browser there is no selection, so
// a dock out there would be ten buttons that could only say "open the browser".
//
// It OVERLAYS the canvas rather than shrinking it. Making it steal width would
// mean renegotiating the render target and the porthole's square crop on every
// resize; overlaying is what the X browser's side panel already does, so the
// dock costs the composition nothing but the pixels it covers.
//
// This file is chrome only — geometry, paint, hover/press, hit test. The tools
// themselves live where their state already lives (licium_latent.cpp owns the
// roll selection and the tune overlay), reached through LiciumCockpitInvoke.

#ifndef LICIUM_COCKPIT_H
#define LICIUM_COCKPIT_H

#ifdef MILKDROP_STANDALONE

#ifdef __cplusplus
extern "C" {
#endif

struct IDirect3DDevice9;

// Tool ids. The ORDER IS THE DOCK ORDER, top to bottom. Ten slots, flat — at
// this count nesting would cost a click to reach anything without buying any
// clarity; the grouping story starts when a slice needs more than one dialog.
enum LiciumTool
{
    LCT_COLOR = 0,   // hue / sat / light / contrast / cycle   (dialog exists)
    LCT_MOTION,      // smear / gain / smooth / band tilt
    LCT_REACT,       // source -> target links + response splines
    LCT_ZOOM,        // pull back past the frame  (wants the porthole canvas)
    LCT_ROTATE,      // view rotation
    LCT_CURVES,      // the shaping-curve shapes, per slot
    LCT_PALETTE,     // the browser's 'p' cycle, made visible
    LCT_GRADE,       // like / no / tags 1-6 / error flag
    LCT_FAMILY,      // promote + graft scan  (retires the CLI step)
    LCT_SEARCH,      // the existing Run-search dialog
    LCT_COUNT
};

// Paint the dock. Called every frame, unconditionally.
void LiciumCockpitRender(struct IDirect3DDevice9* dev, int w, int h);

// Mouse. Client coordinates. Each returns 1 if the dock consumed the event —
// the caller MUST honour that, or a click on a button also drags the window
// (the borderless shell turns a bare WM_LBUTTONDOWN into an HTCAPTION drag).
int  LiciumCockpitMouseMove(int x, int y, int w, int h);
int  LiciumCockpitMouseDown(int x, int y, int w, int h);
int  LiciumCockpitMouseUp(int x, int y, int w, int h);

// Width the dock currently occupies, in pixels, at this canvas size. Anything
// that wants to avoid drawing underneath it asks here rather than recomputing
// the layout (the metrics follow the live UI text scale).
int  LiciumCockpitWidth(int w, int h);

// Implemented in licium_latent.cpp: run the tool. Split this way so the dock
// stays pure chrome and never reaches into the roll/tune state it would have
// to duplicate.
void LiciumCockpitInvoke(int tool);

// Set the one-line status the dock prints under the buttons (the invoke side
// reports refusals through it: "no roll selected", "not built yet").
void LiciumCockpitSetStatus(const wchar_t* s);

#ifdef __cplusplus
}
#endif

#endif // MILKDROP_STANDALONE
#endif // LICIUM_COCKPIT_H
