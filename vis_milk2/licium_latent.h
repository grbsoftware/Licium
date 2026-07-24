// licium_latent.h — the Latent pillar (docs\LATENT_DESIGN.md).
//
// M5a brought the headless load hook Phase B triage drives verdicts through.
// M5b adds Phase C: the X browser — browse the prospector's survivors live,
// like/pass, quick-tag, describe. Votes and words are the raw material the
// growth-path pheromone/promotion loops feed on; nothing here deletes a roll.
// Standalone-only, like every Licium tool.

#ifndef LICIUM_LATENT_H
#define LICIUM_LATENT_H

// Latent generator lineage (IDs LX1-XXXXXXXX, 32-bit seed). The generator
// itself lives in tools\latent_prospect (offline Phase A); this genver is
// recorded here so engine-side code and the tool can't drift silently.
#define LATENT_GENVER 1

// Automation hook: SEND (not post — the payload is a pointer) a WM_COPYDATA
// to the main "Licium" window to hard-cut-load an exact preset file, so the
// Phase B triage script can drive verdicts headlessly:
//   COPYDATASTRUCT.dwData = LICIUM_CD_LOADPRESET
//   COPYDATASTRUCT.lpData = wide (UTF-16) full path, null-terminated
//   COPYDATASTRUCT.cbData = byte length INCLUDING the null
// Returns 1 from the SendMessage when the load was dispatched. Mirrors the
// spirit of WM_LICIUM_BANDIT_ROLL (0x51) / WM_LICIUM_WALL (0x52).
#define LICIUM_CD_LOADPRESET 0x4C4F4144   // 'LOAD'

#ifdef MILKDROP_STANDALONE

#include <windows.h>

struct IDirect3DDevice9;

#ifdef __cplusplus
extern "C" {
#endif

// Automation hook: post to the main window. wParam=1 opens the browser, 0
// closes. Navigation/votes drive through posted keydowns/chars (physical-key
// mimicry), same as a hand on the keyboard.
#define WM_LICIUM_LATENT (WM_APP + 0x53)

// ---- the X browser ----------------------------------------------------------
// Modal like the palette wall: while open it eats every key, and the engine's
// auto-advance is held (plugin.cpp derives m_bPresetLockedByCode from
// LatentBrowserIsOpen). First open shows a landing page; "don't show again"
// persists to licium.ini (bLatentIntroSeen).

void LatentBrowserOpen(void);
void LatentBrowserClose(void);
int  LatentBrowserIsOpen(void);

// WM_CHAR / WM_KEYDOWN while the browser is open (host routes them in). Both
// return 1 when consumed. While the describe field (engine waitstring) is
// active, everything except Enter/Esc returns 0 so the engine's own editor
// handles the text — the browser's labels yield too (the old collision lesson).
int  LatentBrowserHandleChar(int ch);
int  LatentBrowserHandleKeydown(int vk);

// Draw the browser (dim side panel + roll list via the engine's text manager).
// Called from the engine's UI render pass, next to PaletteWallRender.
void LatentBrowserRender(struct IDirect3DDevice9* dev, int w, int h);

// Help > Show intro pages again: clear the landing page's "don't show again".
void LatentResetIntro(void);

// Tune cockpit: the Color group is a MODELESS floating dialog (summoned with
// 'c' in the browser). Because it is modeless, the host message loop must give
// it first crack at each message so its trackbars/edits get keyboard nav -
// standalone_host.cpp calls this and, when non-NULL, IsDialogMessage()es it.
HWND LatentTuneActiveDlg(void);

// Menu accessors (Latent > Open rolls folder / Open notes file).
const wchar_t* LatentGetRollsDir(void);
const wchar_t* LatentGetNotesPath(void);

// Latent > Run search...: modal dialog over the main window that launches
// tools\latent_search.ps1 (scan -> triage -> X refresh) detached. Start is
// pre-filled with the next free seed from Licium\latent_search_ranges.txt.
void LatentSearchDialog(HWND owner);

// Progress for the GUI-launched search: polls the run's log and shows a corner
// bar while scanning / when finished, and phase text in the window title
// throughout. Draws NOTHING during the triage phase - those frames are being
// photographed and measured (the 2026-07-18 stale-frame lesson: never let
// instrumentation into the measurement). Engine calls it each UI pass.
void LatentSearchOverlayRender(struct IDirect3DDevice9* dev, int w, int h);

#ifdef __cplusplus
}
#endif

#endif // MILKDROP_STANDALONE
#endif // LICIUM_LATENT_H
