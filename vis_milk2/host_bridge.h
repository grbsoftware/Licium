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

// Build a fresh top menu bar (File / Effects / Sprites / Playlist / Audio /
// Options / Help). Ownership passes to the window via SetMenu(); the OS frees
// it when the window is destroyed. Called by dxcontext when it creates the
// windowed-mode window.
HMENU HostBuildMenuBar(void);

// Handle a WM_COMMAND whose id is in the host-menu range. Returns 1 if the id
// was ours (and was handled), 0 otherwise so the engine keeps processing it.
int HostHandleMenuCommand(HWND hwnd, int cmd_id);

#ifdef __cplusplus
}
#endif

#endif // MILKDROP_STANDALONE
#endif // HOST_BRIDGE_H
