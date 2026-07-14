// host_spout.h
//
// Standalone-only Spout sender bridge. Publishes each finished MilkDrop frame
// as a Spout sender named "MilkDrop" so a headless receiver (env\spout_grab.exe)
// can grab the real render without a screen grab.
//
// The engine runs a plain IDirect3DDevice9; Spout's shared textures need a
// D3D9Ex device, so this module gives Spout its own Ex device and bridges each
// frame across devices via system memory (see host_spout.cpp).
//
// Compiled ONLY into the standalone exe (MILKDROP_STANDALONE); the Winamp DLL
// build never sees it and stays pristine.

#ifndef HOST_SPOUT_H
#define HOST_SPOUT_H

#ifdef MILKDROP_STANDALONE

#ifdef __cplusplus
extern "C" {
#endif

// Create the "MilkDrop" Spout sender (on Spout's own DX9Ex device). Call once
// after the engine is initialized. No-op on repeat / after a failure.
void HostSpoutInit(void);

// Publish the just-rendered frame. Call right before the engine's Present, so
// the backbuffer still holds this frame (swap effect is DISCARD). engineDevice
// is the engine's IDirect3DDevice9 (passed as void* to keep this header free of
// D3D headers). No-op until HostSpoutInit() has succeeded.
void HostSpoutSendFrame(void *engineDevice);

// Release the sender, bridge surfaces, and Spout's device.
void HostSpoutShutdown(void);

#ifdef __cplusplus
}
#endif

#endif // MILKDROP_STANDALONE
#endif // HOST_SPOUT_H
