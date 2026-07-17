// host_bridge.h
//
// Thin bridge between the pristine MD2 engine and the standalone host's
// white-room menu bar. The engine (dxcontext.cpp, plugin.cpp) calls these two
// host-provided functions; they are implemented in standalone_host.cpp.
//
// Everything here is compiled ONLY into the standalone exe (guarded by
// MILKDROP_STANDALONE, which is defined only in milkdrop_standalone.vcxproj).
// The Winamp DLL build never sees this header's effects and stays pristine.
//
// White-room note: the menu bar is our own "equipment" reconstructed from the
// observed MilkDrop 3.33 menu layout only. No MilkDrop3 source was consulted.

#ifndef HOST_BRIDGE_H
#define HOST_BRIDGE_H

#ifdef MILKDROP_STANDALONE

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

// Build a fresh top menu bar (File / Playlist / Audio / Bandit / Palettes /
// Latent / Help). Ownership passes to the window via SetMenu(); the OS frees
// it when the window is destroyed.
HMENU HostBuildMenuBar(void);

// Called by dxcontext right after it (re)creates the plugin window, in every
// screen mode. Applies the Licium identity (window title + icon — the pristine
// engine captions the window WINDOWCAPTION and uses the old plugin-icon class
// icon) and, when windowed is nonzero, attaches the menu bar. Window recreation
// on each mode switch re-runs this, so the identity survives mode changes.
// iniFile is the engine's resolved config path (milk2.ini) — the host stashes
// it (plus the hwnd) for settings of its own, e.g. the maximized-restore flag;
// the accessors CPluginShell exposes for these are protected.
void HostOnWindowCreated(HWND hwnd, int windowed, const wchar_t *iniFile);

// Handle a WM_COMMAND whose id is in the host-menu range. Returns 1 if the id
// was ours (and was handled), 0 otherwise so the engine keeps processing it.
int HostHandleMenuCommand(HWND hwnd, int cmd_id);

// Licium tool hotkeys. The engine calls this early in HandleRegularKey() with a
// WM_CHAR character, but only in the regular UI mode — the load/settings/mash-up
// modes and text prompts own the printable keys while they are up. Returns 1 if
// the host consumed the key, 0 to let the engine's own hotkeys have it. Generic
// on purpose: Bandit binds 'b' here, Palettes and Latent will bind their own.
// THE AUTHORITATIVE KEY MAP IS docs\HOTKEYS.md — check it before binding.
int HostHandleChar(int ch);

#ifdef __cplusplus
}
#endif

#endif // MILKDROP_STANDALONE
#endif // HOST_BRIDGE_H
