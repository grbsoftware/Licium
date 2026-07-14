// host_spout.cpp
//
// See host_spout.h. Clean-room Spout DX9 sender integration, written against the
// third-party Spout SDK + the pristine engine's device only.
//
// Cross-device bridge (per frame), because the engine's device is a plain
// IDirect3DDevice9 but Spout's shared texture lives on its own IDirect3DDevice9Ex:
//
//   engine RT --GetRenderTargetData--> engine SYSMEM surface
//             --CPU memcpy-->           spout  SYSMEM surface   (Spout's device)
//             --UpdateSurface-->        spout  DEFAULT surface  (StretchRect src)
//             --SendDX9surface-->       spout shared texture -> receiver
//
// A CPU round-trip per frame is fine here: this is a viewer/grabber path, not the
// hot render loop's bottleneck.

#ifdef MILKDROP_STANDALONE

#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <string.h>
#include "spout/SpoutDX9.h"
#include "host_spout.h"

static spoutDX9 g_spout;
static bool     g_init   = false;
static bool     g_failed = false;

static IDirect3DDevice9Ex *g_spDev  = NULL;  // Spout's own Ex device
static IDirect3DDevice9   *g_engDev = NULL;  // engine device the surfaces match
static IDirect3DSurface9  *g_engSys = NULL;  // engine-side sysmem (GetRenderTargetData dst)
static IDirect3DSurface9  *g_spSys  = NULL;  // spout-side sysmem (lockable)
static IDirect3DSurface9  *g_spDef  = NULL;  // spout-side default off-screen (StretchRect src)
static UINT      g_w = 0, g_h = 0;
static D3DFORMAT g_engFmt = D3DFMT_UNKNOWN;

static void HostSpLog(const char *msg)
{
    FILE *f = fopen("host_spout.log", "a");
    if (f) { fprintf(f, "%s\n", msg); fclose(f); }
}

void HostSpoutInit(void)
{
    if (g_init || g_failed) return;
    // Pass a valid window: CreateDeviceEx fails if BOTH focus and device windows
    // are NULL. Spout never renders to it (it only needs the shared-texture
    // device), so the desktop window is fine.
    if (!g_spout.OpenDirectX9(GetDesktopWindow())) { g_failed = true; HostSpLog("OpenDirectX9 failed"); return; }
    g_spDev = g_spout.GetDX9device();
    if (!g_spDev)                { g_failed = true; HostSpLog("GetDX9device null"); return; }
    g_spout.SetSenderName("MilkDrop");
    g_init = true;
    HostSpLog("Spout sender 'MilkDrop' ready");
}

static void ReleaseSurfaces(void)
{
    if (g_engSys) { g_engSys->Release(); g_engSys = NULL; }
    if (g_spSys)  { g_spSys->Release();  g_spSys  = NULL; }
    if (g_spDef)  { g_spDef->Release();  g_spDef  = NULL; }
}

// Lazily (re)create the bridge surfaces when the frame size/format changes. The
// engine-side sysmem surface must match the render target's exact format; the
// spout-side surfaces are forced to X8R8G8B8 (what the receiver expects) -- the
// raw 32bpp bytes copy identically either way.
static bool EnsureSurfaces(IDirect3DDevice9 *engDev, UINT w, UINT h, D3DFORMAT engFmt)
{
    if (g_engSys && w == g_w && h == g_h && engFmt == g_engFmt && engDev == g_engDev)
        return true;

    ReleaseSurfaces();
    g_engDev = engDev; g_w = w; g_h = h; g_engFmt = engFmt;

    if (FAILED(engDev->CreateOffscreenPlainSurface(w, h, engFmt, D3DPOOL_SYSTEMMEM, &g_engSys, NULL)))
        { HostSpLog("engSys create failed"); return false; }
    if (FAILED(g_spDev->CreateOffscreenPlainSurface(w, h, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &g_spSys, NULL)))
        { HostSpLog("spSys create failed"); return false; }
    if (FAILED(g_spDev->CreateOffscreenPlainSurface(w, h, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &g_spDef, NULL)))
        { HostSpLog("spDef create failed"); return false; }
    return true;
}

void HostSpoutSendFrame(void *engineDevice)
{
    if (!g_init || !engineDevice) return;
    IDirect3DDevice9 *engDev = (IDirect3DDevice9 *)engineDevice;

    IDirect3DSurface9 *rt = NULL;
    if (FAILED(engDev->GetRenderTarget(0, &rt)) || !rt) return;
    D3DSURFACE_DESC d;
    rt->GetDesc(&d);

    if (!EnsureSurfaces(engDev, d.Width, d.Height, d.Format)) { rt->Release(); return; }

    // GPU render target -> engine system memory.
    HRESULT hr = engDev->GetRenderTargetData(rt, g_engSys);
    rt->Release();
    if (FAILED(hr)) return;

    // Engine sysmem -> spout sysmem (byte copy, respecting each surface's pitch).
    D3DLOCKED_RECT src, dst;
    if (FAILED(g_engSys->LockRect(&src, NULL, D3DLOCK_READONLY))) return;
    if (FAILED(g_spSys->LockRect(&dst, NULL, 0))) { g_engSys->UnlockRect(); return; }
    const UINT rowBytes = d.Width * 4;
    for (UINT y = 0; y < d.Height; y++)
        memcpy((BYTE *)dst.pBits + (size_t)y * dst.Pitch,
               (BYTE *)src.pBits + (size_t)y * src.Pitch, rowBytes);
    g_spSys->UnlockRect();
    g_engSys->UnlockRect();

    // Spout sysmem -> spout default (same device) -> shared texture / sender.
    if (FAILED(g_spDev->UpdateSurface(g_spSys, NULL, g_spDef, NULL))) return;
    g_spout.SendDX9surface(g_spDef);
}

void HostSpoutShutdown(void)
{
    if (g_init) g_spout.ReleaseDX9sender();
    ReleaseSurfaces();
    g_spout.CloseDirectX9();
    g_init = false;
}

#endif // MILKDROP_STANDALONE
