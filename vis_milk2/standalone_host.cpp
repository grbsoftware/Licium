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
#include <math.h>
#include <stdio.h>
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

// Minimal stand-in for Winamp's localization service. The engine has ~280
// WASABI_API_LNGSTRINGW(...) call sites (preset-name overlay, menus, error
// text) that dispatch through WASABI_API_LNG; with no Winamp there is no real
// service, so without this the first preset-name lookup dereferences a null
// pointer and crashes. The stub returns empty strings instead. (UI/menu text is
// therefore blank in M1 — fine, M1 is menu-free; real localized text is later.)
class HostLanguageStub : public api_language
{
public:
    int _dispatch(int msg, void *retval, void **params = 0, int nparam = 0)
    {
        switch (msg)
        {
        case API_LANGUAGE_GETSTRINGW:
        case API_LANGUAGE_GETSTRINGFROMGUIDW:
        {
            static wchar_t emptyW[1] = { 0 };
            wchar_t *str = 0; size_t maxlen = 0;
            if (nparam >= 5) { str = *(wchar_t **)params[3]; maxlen = *(size_t *)params[4]; }
            wchar_t *out = emptyW;
            if (str && maxlen) { str[0] = 0; out = str; }
            if (retval) *(wchar_t **)retval = out;
            return 1;
        }
        case API_LANGUAGE_GETSTRING:
        case API_LANGUAGE_GETSTRINGFROMGUID:
        {
            static char emptyA[1] = { 0 };
            char *str = 0; size_t maxlen = 0;
            if (nparam >= 5) { str = *(char **)params[3]; maxlen = *(size_t *)params[4]; }
            char *out = emptyA;
            if (str && maxlen) { str[0] = 0; out = str; }
            if (retval) *(char **)retval = out;
            return 1;
        }
        case API_LANGUAGE_LOADLMENU:
        case API_LANGUAGE_LOADLMENUW:
        {
            // Load the menu straight from our own exe resources (plugin.rc),
            // bypassing Winamp's localized loader. Without this, InitGDIStuff's
            // menu load fails -> blank modal error dialog -> init aborts -> black.
            // params: [0]=localized hinst, [1]=original hinst, [2]=resource id.
            HINSTANCE hinst = (nparam >= 2) ? *(HINSTANCE *)params[1] : 0;
            UINT id = (nparam >= 3) ? *(UINT *)params[2] : 0;
            if (!hinst) hinst = GetModuleHandleW(0);
            HMENU m = LoadMenuW(hinst, MAKEINTRESOURCEW(id));
            if (retval) *(HMENU *)retval = m;
            return m ? 1 : 0;
        }
        default:
            return 0; // let _call fall back to its default value (0/NULL)
        }
    }
};
static HostLanguageStub g_langStub;

// ---- Modal-dialog suppressor ------------------------------------------------
// A standalone/headless host must never block on a modal error MessageBox (they
// hijack the screen and stall the render loop). This watchdog thread finds any
// standard dialog ("#32770" = MessageBox/DialogBox) owned by our process and
// closes it, logging that one appeared so failures are still visible.
static volatile bool g_watchdogRun = true;

static BOOL CALLBACK DismissDlgProc(HWND h, LPARAM)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(h))
    {
        wchar_t cls[64] = { 0 };
        GetClassNameW(h, cls, 63);
        if (wcscmp(cls, L"#32770") == 0)
        {
            FILE *lf = fopen("host_dialogs.log", "a");
            if (lf) { fprintf(lf, "auto-dismissed a modal dialog (hwnd=%p)\n", (void *)h); fclose(lf); }
            PostMessageW(h, WM_CLOSE, 0, 0);
        }
    }
    return TRUE;
}

static DWORD WINAPI DialogWatchdog(LPVOID)
{
    while (g_watchdogRun)
    {
        EnumWindows(DismissDlgProc, 0);
        Sleep(80);
    }
    return 0;
}

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

    // Install the localization stub before the plugin can look up any string,
    // and point resource-loading HINSTANCEs at our own exe module (its menu,
    // dialogs and string table are compiled in from plugin.rc).
    WASABI_API_LNG = &g_langStub;
    WASABI_API_ORIG_HINST = hInstance;
    WASABI_API_LNG_HINST  = hInstance;

    // Start the modal-dialog suppressor so init error boxes can't hijack the
    // screen or stall startup.
    CreateThread(0, 0, DialogWatchdog, 0, 0, 0);

    // Hand the plugin a NULL Winamp window + our own hInstance. NULL window ->
    // all its IPC SendMessages are guarded and no-op; hInstance lets its
    // GetModuleFileNameW() find OUR exe and root the config/resource dirs here.
    if (!g_plugin.PluginPreInitialize(0, hInstance)) { g_plugin.PluginQuit(); return 1; }
    if (!g_plugin.PluginInitialize())                { g_plugin.PluginQuit(); return 1; }
    g_bFullyExited = false;

    // M1b(2) bring-up: a synthetic animated waveform so reactive presets light
    // up (proving window + D3D9 + shader + render end-to-end). This is generated
    // in-process — it makes no sound. M1c replaces it with real WASAPI loopback.
    // Winamp waveform format: unsigned char 0..255, 128 = zero line.
    unsigned char waveL[576], waveR[576];
    double phase = 0.0;

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

        for (int i = 0; i < 576; i++)
        {
            double l = sin(phase + i * 0.10) * 95.0 + sin(phase * 0.5 + i * 0.021) * 28.0;
            double r = sin(phase * 1.1 + i * 0.09) * 90.0 + sin(phase * 0.37 + i * 0.017) * 30.0;
            int vl = 128 + (int)l, vr = 128 + (int)r;
            waveL[i] = (unsigned char)(vl < 0 ? 0 : vl > 255 ? 255 : vl);
            waveR[i] = (unsigned char)(vr < 0 ? 0 : vr > 255 ? 255 : vr);
        }
        phase += 0.18;

        // PluginRender() returns nonzero to keep going, zero to stop.
        if (!g_plugin.PluginRender(waveL, waveR)) running = false;
    }

    g_plugin.PluginQuit();
    g_bFullyExited = true;
    return (int)msg.wParam;
}
