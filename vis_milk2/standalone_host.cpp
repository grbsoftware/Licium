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
#include <shlobj.h>
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
#include "wasapi_audio.h"
#include "host_spout.h"
#include "licium_res.h"

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

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
#define HM_AUDIO_SPK_BASE 0xE140   // Speakers (render/loopback) devices, +index
#define HM_AUDIO_MIC_BASE 0xE160   // Microphones (capture) devices, +index

// Command id of the currently-selected audio source (for the menu radio check).
// Starts on Auto = default output, loopback (matches wasapi_audio's default).
static int g_audioCurCmd = HM_AUDIO_AUTO;

// Whether a preset-folder playlist is running (auto-advance on). Controls the
// enabled state of "Stop playlist" across menu rebuilds.
static bool g_playlistActive = false;

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

    // Playlist: point the engine at a preset FOLDER and let its built-in
    // navigation auto-advance through it. Stop freezes on the current preset;
    // it's greyed until a playlist is running.
    HMENU playlist = CreatePopupMenu();
    AppendMenuW(playlist, MF_STRING,             HM_PLAYLIST_LOAD, L"&Load preset folder...");
    AppendMenuW(playlist, MF_STRING | (g_playlistActive ? 0 : MF_GRAYED),
                HM_PLAYLIST_STOP, L"&Stop (hold current)");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)playlist, L"&Playlist");

    // Audio: "Auto" = default system output via loopback (the checked default).
    // Speakers = pick a specific render endpoint (still loopback); Microphones =
    // capture a real input. Submenus are enumerated live; a radio check marks the
    // active source (g_audioCurCmd). Empty kinds stay greyed.
    HMENU audio = CreatePopupMenu();
    AppendMenuW(audio, MF_STRING | (g_audioCurCmd == HM_AUDIO_AUTO ? MF_CHECKED : 0),
                HM_AUDIO_AUTO, L"&Auto (system output)");

    static wchar_t spkNames[WASAPI_MAX_DEVICES][WASAPI_NAME_LEN];
    int nSpk = WasapiAudioListDevices(0, spkNames, WASAPI_MAX_DEVICES);
    HMENU speakers = CreatePopupMenu();
    for (int i = 0; i < nSpk; i++)
        AppendMenuW(speakers, MF_STRING | (g_audioCurCmd == HM_AUDIO_SPK_BASE + i ? MF_CHECKED : 0),
                    HM_AUDIO_SPK_BASE + i, spkNames[i]);
    AppendMenuW(audio, MF_POPUP | (nSpk ? 0 : MF_GRAYED), (UINT_PTR)speakers, L"&Speakers (loopback)");

    static wchar_t micNames[WASAPI_MAX_DEVICES][WASAPI_NAME_LEN];
    int nMic = WasapiAudioListDevices(1, micNames, WASAPI_MAX_DEVICES);
    HMENU mics = CreatePopupMenu();
    for (int i = 0; i < nMic; i++)
        AppendMenuW(mics, MF_STRING | (g_audioCurCmd == HM_AUDIO_MIC_BASE + i ? MF_CHECKED : 0),
                    HM_AUDIO_MIC_BASE + i, micNames[i]);
    AppendMenuW(audio, MF_POPUP | (nMic ? 0 : MF_GRAYED), (UINT_PTR)mics, L"&Microphones");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)audio, L"&Audio");

    // Tool headings (decided 2026-07-16): one top-level slot per Licium tool —
    // Bandit (the shader slot machine), Palettes (vote-on-palettes preferences),
    // Latent (latent-shader-space exploration). Greyed until each tool is
    // re-ported onto this clean-room base; each then becomes a popup whose list
    // invokes the tool and carries its options. (A later milestone may nest them
    // all under one menu — layout is not settled, only the slots are.)
    AppendMenuW(bar, MF_STRING | MF_GRAYED, 0, L"&Bandit");
    AppendMenuW(bar, MF_STRING | MF_GRAYED, 0, L"&Palettes");
    AppendMenuW(bar, MF_STRING | MF_GRAYED, 0, L"&Latent");

    // Help (holds About, per Windows convention): View Docs surfaces the engine's
    // built-in F1 help overlay.
    HMENU help = CreatePopupMenu();
    AppendMenuW(help, MF_STRING, HM_HELP_DOCS, L"&View Docs");
    AppendMenuW(help, MF_SEPARATOR, 0, NULL);
    AppendMenuW(help, MF_STRING, HM_HELP_ABOUT, L"&About Licium...");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"&Help");

    return bar;
}

// Called by dxcontext after every window (re)creation: stamp the Licium
// identity over the pristine engine's defaults (WINDOWCAPTION titles it
// "MilkDrop 2"; the window class carries the old plugin icon), then attach the
// menu bar in windowed mode. The icons live in licium.rc; WM_SETICON overrides
// the class icon per-window, so the engine's RegisterClass stays untouched.
extern "C" void HostOnWindowCreated(HWND hwnd, int windowed)
{
    SetWindowTextW(hwnd, L"Licium");

    HINSTANCE hinst = GetModuleHandleW(0);
    // ("small"/"big" avoided as names: rpcndr.h defines `small` as a macro.)
    HICON hIconBig = (HICON)LoadImageW(hinst, MAKEINTRESOURCEW(IDI_LICIUM), IMAGE_ICON,
                                       0, 0, LR_DEFAULTSIZE | LR_SHARED);
    HICON hIconSm  = (HICON)LoadImageW(hinst, MAKEINTRESOURCEW(IDI_LICIUM), IMAGE_ICON,
                                       GetSystemMetrics(SM_CXSMICON),
                                       GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    if (hIconBig) SendMessageW(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)hIconBig);
    if (hIconSm)  SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSm);

    if (windowed)
        SetMenu(hwnd, HostBuildMenuBar());
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

// Playlist > Load preset folder: pick a folder of .milk presets, point the
// engine's preset list at it, and start auto-advancing through it. MilkDrop's
// "playlist" is really its preset directory plus timed navigation, so we reuse
// the engine's own machinery rather than build a parallel one.
static void HostLoadPlaylistDialog(HWND hwnd)
{
    wchar_t path[MAX_PATH] = L"";
    BROWSEINFOW bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = hwnd;
    bi.lpszTitle = L"Choose a preset folder to play as a playlist";
    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_EDITBOX;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;

    if (SHGetPathFromIDListW(pidl, path) && path[0])
    {
        // Preset dir needs a trailing backslash for the engine's list scan.
        size_t n = wcslen(path);
        if (n && path[n - 1] != L'\\' && n + 1 < MAX_PATH) { path[n] = L'\\'; path[n + 1] = 0; }

        wcsncpy_s(g_plugin.m_szPresetDir, MAX_PATH, path, _TRUNCATE);
        g_plugin.UpdatePresetList();                    // rescan the new folder
        if (g_plugin.m_fTimeBetweenPresets < 1.0f)
            g_plugin.m_fTimeBetweenPresets = 15.0f;     // ensure it actually advances
        g_plugin.m_bPresetLockedByUser = false;         // enable auto-advance
        g_plugin.LoadRandomPreset(2.0f);                // jump into the folder
        g_playlistActive = true;
        EnableMenuItem(GetMenu(hwnd), HM_PLAYLIST_STOP, MF_BYCOMMAND | MF_ENABLED);
    }
    CoTaskMemFree(pidl);
}

extern "C" int HostHandleMenuCommand(HWND hwnd, int cmd_id)
{
    // Audio source selection (Auto / a Speakers device / a Microphones device).
    // Handled ahead of the switch because the device ids are a dynamic range.
    if (cmd_id == HM_AUDIO_AUTO ||
        (cmd_id >= HM_AUDIO_SPK_BASE && cmd_id < HM_AUDIO_SPK_BASE + WASAPI_MAX_DEVICES) ||
        (cmd_id >= HM_AUDIO_MIC_BASE && cmd_id < HM_AUDIO_MIC_BASE + WASAPI_MAX_DEVICES))
    {
        int ok;
        if (cmd_id == HM_AUDIO_AUTO)             ok = WasapiAudioSelect(0, -1);
        else if (cmd_id >= HM_AUDIO_MIC_BASE)    ok = WasapiAudioSelect(1, cmd_id - HM_AUDIO_MIC_BASE);
        else                                     ok = WasapiAudioSelect(0, cmd_id - HM_AUDIO_SPK_BASE);
        if (ok)
        {
            // Move the radio check to the newly-selected item (MF_BYCOMMAND
            // searches submenus recursively, so one call finds it anywhere).
            HMENU bar = GetMenu(hwnd);
            CheckMenuItem(bar, g_audioCurCmd, MF_BYCOMMAND | MF_UNCHECKED);
            g_audioCurCmd = cmd_id;
            CheckMenuItem(bar, g_audioCurCmd, MF_BYCOMMAND | MF_CHECKED);
        }
        return 1;
    }

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
    case HM_PLAYLIST_LOAD:
        HostLoadPlaylistDialog(hwnd);
        return 1;
    case HM_PLAYLIST_STOP:
        g_plugin.m_bPresetLockedByUser = true;   // hold the current preset
        g_playlistActive = false;
        EnableMenuItem(GetMenu(hwnd), HM_PLAYLIST_STOP, MF_BYCOMMAND | MF_GRAYED);
        return 1;
    case HM_HELP_DOCS:
        g_plugin.ToggleHelp();             // engine's built-in F1 help overlay
        return 1;
    case HM_HELP_ABOUT:
        // Credit lines mirror brand\CREDITS.md — Holly Watson (identity) and the
        // Nullsoft/Geiss engine attribution must ship in any distributed build.
        MessageBoxW(hwnd,
            L"Licium\n"
            L"A taste-guided instrument for exploring latent shader space.\n\n"
            L"Logo and visual identity by Holly Watson.\n\n"
            L"Engine: clean-room port of MilkDrop 2 (BSD 3-clause)\n"
            L"Copyright 2005-2013 Nullsoft, Inc. \x2014 originally written by Ryan Geiss.\n"
            L"Spout texture sharing \x00a9 2020-2024 Lynn Jarvis (BSD).",
            L"About Licium", MB_OK | MB_ICONINFORMATION);
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

    // M1c: real audio. Tap the default output device via WASAPI loopback so the
    // visuals react to whatever the machine is actually playing. If it fails to
    // start (no audio device, permissions), fall back to the M1b synthetic
    // waveform so the window still animates instead of going flat.
    bool realAudio = WasapiAudioStart() != 0;

    // M1d: start the Spout sender so a headless receiver (spout_grab) can grab
    // the real render. The engine calls HostSpoutSendFrame() each frame.
    HostSpoutInit();

    // Winamp waveform format: unsigned char 0..255, 128 = zero line. The
    // synthetic generator below is the fallback path only.
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

        // Prefer real WASAPI loopback. When it isn't running (start failed), or
        // hasn't delivered a first packet yet, fall back to the synthetic scope
        // so the window keeps animating.
        if (!realAudio || !WasapiAudioGetWaveform(waveL, waveR))
        {
            for (int i = 0; i < 576; i++)
            {
                double l = sin(phase + i * 0.10) * 95.0 + sin(phase * 0.5 + i * 0.021) * 28.0;
                double r = sin(phase * 1.1 + i * 0.09) * 90.0 + sin(phase * 0.37 + i * 0.017) * 30.0;
                int vl = 128 + (int)l, vr = 128 + (int)r;
                waveL[i] = (unsigned char)(vl < 0 ? 0 : vl > 255 ? 255 : vl);
                waveR[i] = (unsigned char)(vr < 0 ? 0 : vr > 255 ? 255 : vr);
            }
            phase += 0.18;
        }

        // PluginRender() returns nonzero to keep going, zero to stop.
        if (!g_plugin.PluginRender(waveL, waveR)) running = false;
    }

    WasapiAudioStop();
    HostSpoutShutdown();
    g_plugin.PluginQuit();
    g_bFullyExited = true;
    return (int)msg.wParam;
}
