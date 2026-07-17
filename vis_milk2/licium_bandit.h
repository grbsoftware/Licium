// licium_bandit.h
//
// Bandit — Licium's shader slot machine (v1). Press B: roll a seed, generate a
// complete .milk preset from it, save it, load it with a hard cut, and stamp the
// roll with a short ID (L1-XXXX). The ID *is* the seed: rolling the same ID again
// regenerates the same preset byte for byte.
//
// Implemented from docs\BANDIT_DESIGN.md (blessed 2026-07-16). Clean-room: the
// old Bandit lived in the 3.33-contaminated tree, which stays closed — this is
// written fresh against the pristine MD2 base + the M2 buffer engine, and is
// compiled ONLY into the standalone exe (MILKDROP_STANDALONE), so the Winamp DLL
// build never sees it.
//
// The generator version is part of the ID contract: any change to knob math,
// draw order, or the emitted template must bump BANDIT_GENVER (L1 -> L2), because
// old IDs must keep regenerating the presets they originally named.

#ifndef LICIUM_BANDIT_H
#define LICIUM_BANDIT_H

#ifdef MILKDROP_STANDALONE

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

// Generator lineage. 'L' marks the Licium lineage: old-tree IDs (5-XXXX ..
// 26-XXXX) and PS-script IDs (1-XXXX, 2-XXXX) are different lineages and never
// interchange. Bump on ANY generator change (see the header comment).
#define BANDIT_GENVER 1

// Automation hook: post this to the main window to roll an exact seed, so a
// headless run can be driven and verified.
//   wParam = seed (0..0xFFFF), lParam = 0
// Rolling a specific seed from the UI is not a v1 feature; this exists so Claude
// can launch the exe detached, post known seeds, and Spout-grab the results.
#define WM_LICIUM_BANDIT_ROLL (WM_APP + 0x51)

// Roll a preset. Pass a seed in 0..0xFFFF to reproduce an exact roll, or -1 to
// draw a fresh random seed. Generates the preset text, saves it to
// <presets>\bandit\bandit_L1-XXXX.milk, loads it with a hard cut, copies the ID
// to the clipboard, and arms the corner overlay. Returns 1 on success.
// Must be called on the render thread (it drives g_plugin directly).
int BanditRoll(int seed);

// The current roll's ID ("L1-A3F0") for the corner overlay, or NULL when no roll
// is current. Cleared by BanditOnPresetLoaded() when something else takes over.
const wchar_t* BanditGetOverlayId(void);

// Called by the engine (guarded) whenever a preset file is loaded. Anything that
// isn't the roll we just loaded means the roll is no longer on screen, so the
// overlay clears — the blessed lifetime is "until the next preset change".
void BanditOnPresetLoaded(const wchar_t* szPresetPath);

// Put the current roll's ID on the clipboard (menu: Bandit > Copy last ID).
// Returns 1 if there was an ID to copy.
int BanditCopyIdToClipboard(void);

// Full path of the rolls folder (<presets>\bandit\), for Bandit > Open rolls
// folder. Valid after the first roll resolves the engine's preset dir.
const wchar_t* BanditGetRollsDir(void);

#ifdef __cplusplus
}
#endif

#endif // MILKDROP_STANDALONE
#endif // LICIUM_BANDIT_H
