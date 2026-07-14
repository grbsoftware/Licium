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
#include <commdlg.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "vis.h"
#include "plugin.h"
#include "defines.h"
#include "utility.h"
#include "host_bridge.h"

#pragma comment(lib, "comdlg32.lib")

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
// hijack the screen and stall the render loop) DURING INIT. This watchdog thread
// finds any standard dialog ("#32770" = MessageBox/DialogBox) owned by our
// process and closes it, logging that one appeared so failures are still visible.
//
// It is ARMED only until init completes: once the render loop is up, dialogs are
// legitimate user actions (Open preset, About box, menu-driven) and must NOT be
// auto-dismissed. g_suppressDialogs is cleared right after PluginInitialize.
static volatile bool g_watchdogRun     = true;
static volatile bool g_suppressDialogs = true;

static BOOL CALLBACK DismissDlgProc(HWND h, LPARAM)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (g_suppressDialogs && pid == GetCurrentProcessId() && IsWindowVisible(h))
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

// ---- White-room menu bar ----------------------------------------------------
// Reconstructed from the observed MilkDrop 3.33 top menu bar ONLY (no MD3 source
// was read). We own the equipment; drop-downs are populated only for features
// this port actually implements, and unbuilt items are greyed as placeholders.
//
// Fresh command ids in a private range (0xE1xx) so they never collide with the
// engine's Winamp-derived WM_COMMAND ids (ID_VIS_* ~40000) or plugin.rc ids
// (<=~40010). The engine forwards WM_COMMAND to HostHandleMenuCommand first;
// ids outside our range fall through to its own handlers unchanged.
#define HM_FILE_NEW       0xE101
#define HM_FILE_OPEN      0xE102
#define HM_FILE_ADDFAV    0xE103
#define HM_FILE_EXIT      0xE104
#define HM_PLAYLIST_LOAD  0xE110
#define HM_PLAYLIST_STOP  0xE111
#define HM_AUDIO_AUTO     0xE120
#define HM_HELP_DOCS      0xE130
#define HM_HELP_ABOUT     0xE131

extern "C" HMENU HostBuildMenuBar(void)
{
    HMENU bar = CreateMenu();

    // File: New | Open | Add to Favorites (deferred) | Exit
    HMENU file = CreatePopupMenu();
    AppendMenuW(file, MF_STRING,            HM_FILE_NEW,    L"&New");
    AppendMenuW(file, MF_STRING,            HM_FILE_OPEN,   L"&Open...");
    AppendMenuW(file, MF_STRING | MF_GRAYED, HM_FILE_ADDFAV, L"Add to &Favorites");
    AppendMenuW(file, MF_SEPARATOR, 0, NULL);
    AppendMenuW(file, MF_STRING,            HM_FILE_EXIT,   L"E&xit");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&File");

    // (3.33 had Effects and Sprites here; we are not porting those features, so
    // they are omitted rather than shown as dead placeholders. New categories can
    // be added later when the features behind them exist.)

    // Playlist: loads *preset* playlists (.milk lists). Stop greyed until one runs.
    HMENU playlist = CreatePopupMenu();
    AppendMenuW(playlist, MF_STRING,            HM_PLAYLIST_LOAD, L"&Load playlist...");
    AppendMenuW(playlist, MF_STRING | MF_GRAYED, HM_PLAYLIST_STOP, L"&Stop playlist");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)playlist, L"&Playlist");

    // Audio: Auto (default loopback, checked). Device pickers land in M1c (WASAPI).
    HMENU audio = CreatePopupMenu();
    AppendMenuW(audio, MF_STRING | MF_CHECKED, HM_AUDIO_AUTO, L"&Auto");
    HMENU speakers = CreatePopupMenu();
    AppendMenuW(audio, MF_POPUP | MF_GRAYED, (UINT_PTR)speakers, L"&Speakers");
    HMENU mics = CreatePopupMenu();
    AppendMenuW(audio, MF_POPUP | MF_GRAYED, (UINT_PTR)mics, L"&Microphones");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)audio, L"&Audio");

    // Options: blocked on the aggregate preset-dials feature -> greyed for now.
    AppendMenuW(bar, MF_STRING | MF_GRAYED, 0, L"&Options");

    // Help (holds About, per Windows convention): View Docs surfaces the engine's
    // built-in F1 help overlay.
    HMENU help = CreatePopupMenu();
    AppendMenuW(help, MF_STRING, HM_HELP_DOCS, L"&View Docs");
    AppendMenuW(help, MF_SEPARATOR, 0, NULL);
    AppendMenuW(help, MF_STRING, HM_HELP_ABOUT, L"&About MilkDrop...");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"&Help");

    return bar;
}

// File > Open: pick a .milk preset and load it with a short blend.
static void HostOpenPresetDialog(HWND hwnd)
{
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFilter = L"MilkDrop presets (*.milk)\0*.milk\0All files (*.*)\0*.*\0";
    ofn.lpstrFile   = file;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = L"Open preset";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (GetOpenFileNameW(&ofn))
        g_plugin.LoadPreset(file, 2.0f);
}

extern "C" int HostHandleMenuCommand(HWND hwnd, int cmd_id)
{
    switch (cmd_id)
    {
    case HM_FILE_NEW:
        g_plugin.LoadRandomPreset(2.0f);   // bring up a fresh visual
        return 1;
    case HM_FILE_OPEN:
        HostOpenPresetDialog(hwnd);
        return 1;
    case HM_FILE_EXIT:
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 1;
    case HM_HELP_DOCS:
        g_plugin.ToggleHelp();             // engine's built-in F1 help overlay
        return 1;
    case HM_HELP_ABOUT:
        MessageBoxW(hwnd,
            L"MilkDrop 2 \x2014 clean-room standalone port\n\n"
            L"Based on the BSD-licensed MilkDrop 2 (Nullsoft / Ryan Geiss).\n"
            L"Runs headless \x2014 no Winamp required.",
            L"About MilkDrop", MB_OK | MB_ICONINFORMATION);
        return 1;
    default:
        return 0;   // not one of ours; let the engine handle it
    }
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

    // Init succeeded and the window/menu are up: stop auto-dismissing modal
    // dialogs so the menu's Open/About boxes work as real user interactions.
    g_suppressDialogs = false;

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
