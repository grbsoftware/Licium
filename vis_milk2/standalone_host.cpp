// standalone_host.cpp
//
// Original standalone host for the clean-room MD2 port. Replaces Winamp as the
// process that owns the plugin: it hands the pristine plugin a NULL "Winamp
// window" (so every WM_WA_IPC call no-ops through the plugin's own guards, and
// the plugin resolves its config/resource dirs relative to THIS exe), drives
// the four-call lifecycle, and feeds waveform data each frame.
//
// NOTE: not a line of the 3.33-derived Milkdrop2PcmVisualizer.cpp was used here;
// this is written against the pristine BSD plugin interface (pluginshell.h) only.
//
// M1b scope: bring-up with silent audio (proves window + D3D9 + render loop in
// our own exe). WASAPI loopback audio arrives in M1c.

#include "api.h"
#include <windows.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include "vis.h"
#include "plugin.h"
#include "defines.h"
#include "utility.h"

// --- Globals the engine links against ---------------------------------------
// Normally defined in vis.cpp (the Winamp entry), which the standalone EXE
// excludes from its build; we provide them here instead.
CPlugin   g_plugin;
_locale_t g_use_C_locale = 0;
char      keyMappings[8];
bool      g_bFullyExited = true;

// Wasabi localisation services. Unused in the standalone render path (kept NULL,
// exactly as vis.cpp initializes them before Winamp wires them up). Menu/config
// text goes through these, but M1 is menu-free.
api_service     *WASABI_API_SVC    = 0;
api_language    *WASABI_API_LNG    = 0;
api_application *WASABI_API_APP     = 0;
api_syscb       *WASABI_API_SYSCB   = 0;
HINSTANCE WASABI_API_LNG_HINST = 0, WASABI_API_ORIG_HINST = 0;

// The rand function the engine expects the host (Winamp) to supply. Declared
// extern "C" in plugin.h; give it a real implementation up front so nothing
// calls through a null pointer.
static int host_rand_fn(void) { return (rand() ^ (rand() << 8)) & 0x7fffffff; }
int (*warand)(void) = host_rand_fn;

// The engine references the Winamp module descriptor (normally in vis.cpp). In
// standalone we never dispatch through it — we call PluginRender() directly — so
// a zeroed instance with null entry-point pointers is all the linker needs.
winampVisModule mod1 =
{
    MODULEDESC,
    NULL,   // hwndParent
    NULL,   // hDllInstance
    0,      // sRate
    0,      // nCh
    0,      // latencyMS
    10,     // delayMS
    0,      // spectrumNch
    2,      // waveformNch
    { 0, }, // spectrumData
    { 0, }, // waveformData
    NULL,   // config
    NULL,   // init
    NULL,   // render1
    NULL    // quit
};

// ----------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE /*hPrev*/, PSTR /*cmd*/, int /*show*/)
{
    srand((unsigned)GetTickCount());

    // Hand the plugin a NULL Winamp window + our own hInstance. NULL window ->
    // all its IPC SendMessages are guarded and no-op; hInstance lets its
    // GetModuleFileNameW() find OUR exe and root the config/resource dirs here.
    if (!g_plugin.PluginPreInitialize(0, hInstance)) { g_plugin.PluginQuit(); return 1; }
    if (!g_plugin.PluginInitialize())                { g_plugin.PluginQuit(); return 1; }
    g_bFullyExited = false;

    // Silent waveform for M1b bring-up: 128 is the zero line in Winamp's 0..255
    // sample space. (M1c replaces this with WASAPI loopback each frame.)
    unsigned char waveL[576], waveR[576];
    memset(waveL, 128, sizeof(waveL));
    memset(waveR, 128, sizeof(waveR));

    MSG msg;
    memset(&msg, 0, sizeof(msg));
    bool running = true;
    while (running)
    {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!running) break;

        // PluginRender() returns nonzero to keep going, zero to stop.
        if (!g_plugin.PluginRender(waveL, waveR)) running = false;
    }

    g_plugin.PluginQuit();
    g_bFullyExited = true;
    return (int)msg.wParam;
}
