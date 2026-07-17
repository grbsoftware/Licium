// licium_palettes.h
//
// Palettes — Licium's taste pillar (v1). Press V: a modal wall of rolled
// palette ramps; like/pass votes accumulate into a pool that Bandit dresses
// its rolls from. Implemented from docs\PALETTES_DESIGN.md (blessed
// 2026-07-16). Clean-room: the old palette system stays closed; the curated
// library is regenerated from public sources (tools\gen_palette_lib.ps1).
//
// Standalone-only (MILKDROP_STANDALONE); the Winamp DLL never sees this.

#ifndef LICIUM_PALETTES_H
#define LICIUM_PALETTES_H

#ifdef MILKDROP_STANDALONE

#include <windows.h>

struct IDirect3DDevice9;

#ifdef __cplusplus
extern "C" {
#endif

// Palette generator lineage (ID P1-XXXX). Bump on ANY change to strategy
// dispatch, knob math, or draw order — old palette IDs must keep regenerating
// the colors they named.
#define PALETTE_GENVER 1

// Automation hook: post to the main window. wParam=1 opens the wall, 0 closes.
#define WM_LICIUM_WALL (WM_APP + 0x52)

// Roll palette #seed deterministically: 3..5 sRGB colors (0..1), sorted
// ascending by perceived luminance (deep -> accent). strategyOut (optional):
// 0=oklch 1=curated 2=cosine 3=hsl. Returns the color count.
int PaletteRoll(unsigned short seed, float rgb[5][3], int* strategyOut);

// ---- the voting wall ---------------------------------------------------------

void PaletteWallOpen(void);
void PaletteWallClose(void);
int  PaletteWallIsOpen(void);

// WM_CHAR / WM_KEYDOWN while the wall is open (host routes them in). Both
// return 1 when consumed. The wall eats every char while open; keydown handles
// arrows/PgUp/PgDn/Esc and swallows Space's keydown half (the char half votes —
// the WM_KEYDOWN + WM_CHAR duality: each action in exactly one message type).
int  PaletteWallHandleChar(int ch);
int  PaletteWallHandleKeydown(int vk);

// Draw the wall (dim + swatch grid + labels via the engine's text manager).
// Called at the end of the engine's UI render pass with the current device and
// TRUE client size. No-op when closed.
void PaletteWallRender(struct IDirect3DDevice9* dev, int w, int h);

// ---- the liked pool (Bandit dressing reads these) -----------------------------

int  PalettePoolCount(void);
// Fetch liked palette #i (0-based). Returns color count (0 if out of range).
int  PalettePoolGet(int i, float rgb[5][3]);

// Full path of palette_votes.txt (for the menu's "Open votes file").
const wchar_t* PaletteGetVotesPath(void);

#ifdef __cplusplus
}
#endif

#endif // MILKDROP_STANDALONE
#endif // LICIUM_PALETTES_H
