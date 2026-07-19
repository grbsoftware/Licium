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
#include <shellapi.h>   // ShellExecuteW — Bandit > Open rolls folder
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
#include "licium_version.h"
#include "licium_bandit.h"
#include "licium_palettes.h"
#include "licium_latent.h"

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

// ---- F1 help overlay ----------------------------------------------------------
// The engine draws whatever g_szHelp points at (pluginshell.cpp, per frame); by
// default that's its MilkDrop-era text resource, which documents keys we don't
// ship and points at a milkdrop .html file. After init, WinMain repoints it at
// this Licium text instead — only keys that actually work in the standalone
// build (Winamp-remote keys like U-shuffle are omitted; custom-message and
// sprite keys wait until those features are surfaced). THE AUTHORITATIVE KEY
// MAP IS docs\HOTKEYS.md — update both together, and check that registry before
// assigning any new tool hotkey (Bandit/Palettes/Latent).
extern wchar_t* g_szHelp;
extern int      g_szHelp_W;

static const wchar_t LICIUM_HELP_TEXT[] =
    L"LICIUM KEYS\n"
    L"\n"
    L"  F1           show / hide this help\n"
    L"  Alt+Enter    toggle fullscreen\n"
    L"  Esc          close help/menus; leave fullscreen\n"
    L"  Scroll Lock  lock current preset (blocks auto-advance)\n"
    L"\n"
    L"PRESETS\n"
    L"  Space        next preset (soft blend)\n"
    L"  H            next preset (hard cut)\n"
    L"  Backspace    previous preset\n"
    L"  R            toggle random / sequential order\n"
    L"  A            random preset with random warp+comp shaders\n"
    L"  D            cycle shader locks (comp / warp / both / none)\n"
    L"  ! / @        randomize warp / comp shader only\n"
    L"  L            preset load menu       F8  change preset folder\n"
    L"  M            engine settings menu\n"
    L"  S            save preset as...      + / -  rate preset\n"
    L"\n"
    L"TOOLS\n"
    L"  B            Bandit: roll a brand-new visual (its L1-XXXX ID is the\n"
    L"               seed - roll that ID again for the same visual, and the\n"
    L"               roll is saved under presets\\bandit\\)\n"
    L"  V            Palettes: the voting wall - like/pass color palettes;\n"
    L"               Bandit dresses rolls in palettes you liked\n"
    L"  X            Latent: browse the shader prospector's survivors live -\n"
    L"               vote, quick-tag (1-6), and D to describe what you see\n"
    L"\n"
    L"OVERLAYS\n"
    L"  F2 title   F3 time   F4 preset info   F5 fps   F6 rating   N debug\n"
    L"\n"
    L"TWEAK THE CURRENT PRESET\n"
    L"  i / I        zoom\n"
    L"  W / w        waveform mode     E / e  wave brightness   J / j  wave size\n"
    L"  O / o        warp amount       G / g  gamma   Q / q  echo zoom   F  echo flip\n"
    L"  < >          rotation          [ ] { }  push x / y\n"
    L"\n"
    L"Audio source, playlists and tools live in the menu bar.";

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

// ---- Splash / About panel ----------------------------------------------------
// Photoshop-style splash: Holly Watson's wordmark (embedded PNG, licium.rc) on
// the left, app info on the right (name, version, build date, credits) with a
// Continue button. Shown at startup in a borderless topmost window, centered;
// non-blocking (the engine inits and renders behind it), and it HOLDS until
// dismissed — Continue click, any click on the panel, or Esc/Enter when it has
// focus — then fades out. Help > About shows the same panel, so the version and
// the credit lines (Holly Watson; Nullsoft/Geiss BSD; Spout) have one home.
// Version text comes from licium_version.h, the same header licium.rc stamps
// into the exe's VERSIONINFO — the two cannot drift apart.

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#define SPLASH_FADE_MS 400

static HWND    g_splashWnd       = 0;
static HBITMAP g_splashBmp       = 0;
static int     g_splashW         = 0;
static int     g_splashH         = 0;
static DWORD   g_splashDismissAt = 0;   // nonzero = fading out since this tick

static void HostSplashBeginDismiss(void)
{
    if (g_splashWnd && !g_splashDismissAt)
        g_splashDismissAt = GetTickCount();
}

static LRESULT CALLBACK SplashWndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc  = BeginPaint(h, &ps);
        HDC mem = CreateCompatibleDC(dc);
        HGDIOBJ old = SelectObject(mem, g_splashBmp);
        BitBlt(dc, 0, 0, g_splashW, g_splashH, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
        // The Continue button is the affordance, but (splash-screen convention)
        // a click anywhere dismisses.
        HostSplashBeginDismiss();
        return 0;
    case WM_KEYDOWN:
        if (w == VK_ESCAPE || w == VK_RETURN || w == VK_SPACE)
        { HostSplashBeginDismiss(); return 0; }
        break;
    }
    return DefWindowProcW(h, m, w, l);
}

// Draw everything (art, info text, button) once into a plain GDI bitmap so the
// window's WM_PAINT is a bare BitBlt and GDI+ can be shut down right away.
static HBITMAP HostSplashRender(HINSTANCE hInstance, int imgSize, int panelW)
{
    // Pull the PNG bytes out of our resources; any failure just means no splash.
    HRSRC res = FindResourceW(hInstance, MAKEINTRESOURCEW(IDR_LICIUM_SPLASH), (LPCWSTR)RT_RCDATA);
    if (!res) return 0;
    DWORD  cb   = SizeofResource(hInstance, res);
    HGLOBAL hres = LoadResource(hInstance, res);
    void  *bytes = hres ? LockResource(hres) : 0;
    if (!bytes || !cb) return 0;

    ULONG_PTR gpToken = 0;
    Gdiplus::GdiplusStartupInput gpsi;
    if (Gdiplus::GdiplusStartup(&gpToken, &gpsi, 0) != Gdiplus::Ok) return 0;

    HBITMAP bmp = 0;
    {
        // (IStream wants its own copy of the data on an HGLOBAL.)
        HGLOBAL hcopy = GlobalAlloc(GMEM_MOVEABLE, cb);
        if (!hcopy) { Gdiplus::GdiplusShutdown(gpToken); return 0; }
        memcpy(GlobalLock(hcopy), bytes, cb);
        GlobalUnlock(hcopy);
        IStream *stream = 0;
        if (CreateStreamOnHGlobal(hcopy, TRUE, &stream) != S_OK)
        { GlobalFree(hcopy); Gdiplus::GdiplusShutdown(gpToken); return 0; }

        Gdiplus::Bitmap png(stream);
        stream->Release();
        if (png.GetLastStatus() == Gdiplus::Ok)
        {
            BITMAPINFO bi;
            ZeroMemory(&bi, sizeof(bi));
            bi.bmiHeader.biSize     = sizeof(bi.bmiHeader);
            bi.bmiHeader.biWidth    = imgSize + panelW;
            bi.bmiHeader.biHeight   = -imgSize;
            bi.bmiHeader.biPlanes   = 1;
            bi.bmiHeader.biBitCount = 32;
            void *bits = 0;
            bmp = CreateDIBSection(0, &bi, DIB_RGB_COLORS, &bits, 0, 0);
            if (bmp)
            {
                HDC mem = CreateCompatibleDC(0);
                HGDIOBJ oldbmp = SelectObject(mem, bmp);

                // Black canvas (matches the wordmark's background), art at left.
                RECT all = { 0, 0, imgSize + panelW, imgSize };
                FillRect(mem, &all, (HBRUSH)GetStockObject(BLACK_BRUSH));
                {
                    Gdiplus::Graphics gfx(mem);
                    gfx.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                    gfx.DrawImage(&png, 0, 0, imgSize, imgSize);
                }

                // Info panel. All sizes scale off the panel so it lays out the
                // same at any screen resolution. (Layout budget: the text block
                // must end above the Continue button at bottom-right — keep the
                // line count/spacing modest if adding lines.)
                int pad   = panelW / 12;
                int x     = imgSize + pad;
                int right = imgSize + panelW - pad;
                // The wordmark art already carries the name, so the text-side
                // "Licium" is just a heading: only slightly larger than the
                // attribution lines, not a second logo.
                int yTitle = pad;
                int hBody  = panelW / 24;
                int hTitle = hBody * 4 / 3;
                int line   = hBody * 5 / 4;

                SetBkMode(mem, TRANSPARENT);
                HFONT fTitle = CreateFontW(hTitle, 0,0,0, FW_BOLD,  0,0,0, DEFAULT_CHARSET,
                                           0,0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
                HFONT fBody  = CreateFontW(hBody,  0,0,0, FW_NORMAL,0,0,0, DEFAULT_CHARSET,
                                           0,0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

                HGDIOBJ oldfont = SelectObject(mem, fTitle);
                SetTextColor(mem, RGB(255,255,255));
                TextOutW(mem, x, yTitle, L"Licium", 6);

                SelectObject(mem, fBody);
                int y = yTitle + hTitle + hBody;
                SetTextColor(mem, RGB(200,200,200));
                wchar_t ver[128];
                swprintf(ver, L"Version %s  \x00b7  built %S", LICIUM_VERSION_WSTR, __DATE__);
                TextOutW(mem, x, y, ver, lstrlenW(ver));                        y += line * 2;

                SetTextColor(mem, RGB(255,255,255));
                TextOutW(mem, x, y, L"Logo & visual identity by Holly Watson", 38); y += line * 2;

                SetTextColor(mem, RGB(150,150,150));
                TextOutW(mem, x, y, L"Engine: MilkDrop 2 (BSD 3-clause)", 33);  y += line;
                TextOutW(mem, x, y, L"\x00a9 2001-2013 Nullsoft, Inc. \x2014 Ryan Geiss", 40); y += line;
                TextOutW(mem, x, y, L"Spout \x00a9 Lynn Jarvis (BSD)", 26);

                // Continue button, bottom-right of the panel. Purely the visual
                // affordance — a click anywhere on the splash dismisses it.
                int btnH = panelW / 8, btnW = panelW / 3;
                RECT b;
                b.right  = right;
                b.bottom = imgSize - pad;
                b.left   = right - btnW;
                b.top    = b.bottom - btnH;
                HBRUSH frame = CreateSolidBrush(RGB(255,255,255));
                FrameRect(mem, &b, frame);
                InflateRect(&b, -1, -1);
                FrameRect(mem, &b, frame);
                DeleteObject(frame);
                SetTextColor(mem, RGB(255,255,255));
                DrawTextW(mem, L"Continue", 8, &b, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                SelectObject(mem, oldfont);
                DeleteObject(fTitle);
                DeleteObject(fBody);
                SelectObject(mem, oldbmp);
                DeleteDC(mem);
            }
        }
    }
    Gdiplus::GdiplusShutdown(gpToken);
    return bmp;
}

static void HostSplashDestroy(void)
{
    if (g_splashWnd) { DestroyWindow(g_splashWnd); g_splashWnd = 0; }
    if (g_splashBmp) { DeleteObject(g_splashBmp);  g_splashBmp = 0; }
    g_splashDismissAt = 0;
}

// Show the splash/About panel. activate=0 at startup (the engine window keeps
// focus, so init isn't disturbed); activate=1 from Help > About (so Esc/Enter
// can dismiss it). Re-callable: a second call while visible just re-fronts it.
static void HostSplashShow(HINSTANCE hInstance, int activate)
{
    if (g_splashWnd)
    {
        g_splashDismissAt = 0;   // was mid-fade? restore it
        SetLayeredWindowAttributes(g_splashWnd, 0, 255, LWA_ALPHA);
        SetWindowPos(g_splashWnd, HWND_TOPMOST, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW);
        return;
    }

    int imgSize = GetSystemMetrics(SM_CYSCREEN) * 2 / 5;   // 40% of screen height
    int panelW  = imgSize * 6 / 5;
    g_splashBmp = HostSplashRender(hInstance, imgSize, panelW);
    if (!g_splashBmp) return;
    g_splashW = imgSize + panelW;
    g_splashH = imgSize;

    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc   = SplashWndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(0, IDC_ARROW);
    wc.lpszClassName = L"LiciumSplash";
    RegisterClassW(&wc);   // fails harmlessly if already registered (About re-show)

    int scx = GetSystemMetrics(SM_CXSCREEN), scy = GetSystemMetrics(SM_CYSCREEN);
    g_splashWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,   // toolwindow: no taskbar button
        L"LiciumSplash", L"Licium", WS_POPUP,
        (scx - g_splashW) / 2, (scy - g_splashH) / 2,
        g_splashW, g_splashH, 0, 0, hInstance, 0);
    if (!g_splashWnd) { DeleteObject(g_splashBmp); g_splashBmp = 0; return; }

    SetLayeredWindowAttributes(g_splashWnd, 0, 255, LWA_ALPHA);
    ShowWindow(g_splashWnd, activate ? SW_SHOW : SW_SHOWNA);
    UpdateWindow(g_splashWnd);            // synchronous first paint (we own the thread)
}

// Called once per render-loop iteration: no-op while the splash holds; once a
// dismiss begins, fade out and tear down.
static void HostSplashTick(void)
{
    if (!g_splashWnd || !g_splashDismissAt) return;
    DWORD t = GetTickCount() - g_splashDismissAt;
    if (t < SPLASH_FADE_MS)
        SetLayeredWindowAttributes(g_splashWnd, 0,
            (BYTE)(255 - 255 * t / SPLASH_FADE_MS), LWA_ALPHA);
    else
        HostSplashDestroy();
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
#define HM_BANDIT_ROLL    0xE180   // (0xE140/0xE160 are +index device ranges of 16)
#define HM_BANDIT_COPYID  0xE181
#define HM_BANDIT_FOLDER  0xE182
#define HM_PAL_WALL       0xE190
#define HM_PAL_VOTES      0xE191
#define HM_LATENT_EXPLORE 0xE1A0
#define HM_LATENT_FOLDER  0xE1A1
#define HM_LATENT_NOTES   0xE1A2
#define HM_LATENT_SEARCH  0xE1A3   // Run search... dialog (latent_search.ps1)
#define HM_HELP_INTRO     0xE132   // Show intro pages again (Help)

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
    //
    // Bandit is live as of v1: Roll rolls a fresh seed; the ID it stamps on the
    // roll is the seed, so the rolls folder + the ID are the whole reproducibility
    // story. Copy last ID / Open rolls folder are greyed until a roll exists.
    HMENU bandit = CreatePopupMenu();
    AppendMenuW(bandit, MF_STRING, HM_BANDIT_ROLL, L"&Roll\tB");
    AppendMenuW(bandit, MF_SEPARATOR, 0, NULL);
    AppendMenuW(bandit, MF_STRING | (BanditGetOverlayId() ? 0 : MF_GRAYED),
                HM_BANDIT_COPYID, L"&Copy last ID");
    AppendMenuW(bandit, MF_STRING, HM_BANDIT_FOLDER, L"&Open rolls folder");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)bandit, L"&Bandit");

    // Palettes is live as of v1: the voting wall trains the liked pool that
    // Bandit dresses rolls from.
    HMENU palettes = CreatePopupMenu();
    AppendMenuW(palettes, MF_STRING, HM_PAL_WALL,  L"&Voting wall\tV");
    AppendMenuW(palettes, MF_SEPARATOR, 0, NULL);
    AppendMenuW(palettes, MF_STRING, HM_PAL_VOTES, L"&Open votes file");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)palettes, L"&Palettes");

    // Latent is live as of M5b: Explore opens the X browser over the rolls the
    // offline prospector (tools\latent_prospect) + triage emitted.
    HMENU latent = CreatePopupMenu();
    AppendMenuW(latent, MF_STRING, HM_LATENT_EXPLORE, L"&Explore\tX");
    AppendMenuW(latent, MF_STRING, HM_LATENT_SEARCH,  L"&Run search...");
    AppendMenuW(latent, MF_SEPARATOR, 0, NULL);
    AppendMenuW(latent, MF_STRING, HM_LATENT_FOLDER, L"&Open rolls folder");
    AppendMenuW(latent, MF_STRING, HM_LATENT_NOTES,  L"Open notes &file");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)latent, L"&Latent");

    // Help (holds About, per Windows convention): View Docs surfaces the engine's
    // built-in F1 help overlay.
    HMENU help = CreatePopupMenu();
    AppendMenuW(help, MF_STRING, HM_HELP_DOCS, L"&View Docs");
    AppendMenuW(help, MF_STRING, HM_HELP_INTRO, L"Show &intro pages again");
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
// Captured from the engine each time it (re)creates its window: the live main
// hwnd and the resolved config ini path (CPluginShell's accessors for these
// are protected, and duplicating the engine's ini-path logic would drift).
static HWND    g_hostMainWnd = 0;
static wchar_t g_hostIniFile[MAX_PATH] = { 0 };

extern "C" void HostOnWindowCreated(HWND hwnd, int windowed, const wchar_t *iniFile)
{
    g_hostMainWnd = hwnd;
    if (iniFile) wcsncpy_s(g_hostIniFile, MAX_PATH, iniFile, _TRUNCATE);

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

extern "C" int HostHandleChar(int ch)
{
    // An open modal owns every char (the wall votes with Y/N/U and closes on V;
    // the latent browser votes/tags/describes and closes on X).
    if (PaletteWallIsOpen())
        return PaletteWallHandleChar(ch);
    if (LatentBrowserIsOpen())
        return LatentBrowserHandleChar(ch);

    switch (ch)
    {
    case 'b':
    case 'B':
        BanditRoll(-1);   // -1 = draw a fresh random seed
        return 1;
    case 'v':
    case 'V':
        PaletteWallOpen();
        return 1;
    case 'x':
    case 'X':
        LatentBrowserOpen();
        return 1;
    default:
        return 0;         // not a tool key; the engine's hotkeys get it
    }
}

extern "C" int HostHandleKeydown(int vk)
{
    // Only modals consume virtual keys; with nothing open the engine keeps
    // every key it already had.
    if (PaletteWallIsOpen())
        return PaletteWallHandleKeydown(vk);
    if (LatentBrowserIsOpen())
        return LatentBrowserHandleKeydown(vk);
    return 0;
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
    case HM_BANDIT_ROLL:
        BanditRoll(-1);                    // -1 = draw a fresh random seed
        return 1;
    case HM_BANDIT_COPYID:
        BanditCopyIdToClipboard();
        return 1;
    case HM_BANDIT_FOLDER:
        ShellExecuteW(hwnd, L"open", BanditGetRollsDir(), NULL, NULL, SW_SHOWNORMAL);
        return 1;
    case HM_PAL_WALL:
        PaletteWallOpen();
        return 1;
    case HM_PAL_VOTES:
        ShellExecuteW(hwnd, L"open", PaletteGetVotesPath(), NULL, NULL, SW_SHOWNORMAL);
        return 1;
    case HM_LATENT_EXPLORE:
        LatentBrowserOpen();
        return 1;
    case HM_LATENT_SEARCH:
        LatentSearchDialog(hwnd);
        return 1;
    case HM_LATENT_FOLDER:
        ShellExecuteW(hwnd, L"open", LatentGetRollsDir(), NULL, NULL, SW_SHOWNORMAL);
        return 1;
    case HM_LATENT_NOTES:
        ShellExecuteW(hwnd, L"open", LatentGetNotesPath(), NULL, NULL, SW_SHOWNORMAL);
        return 1;
    case HM_HELP_INTRO:
        LatentResetIntro();
        return 1;
    case HM_HELP_DOCS:
        g_plugin.ToggleHelp();             // engine's built-in F1 help overlay
        return 1;
    case HM_HELP_ABOUT:
        // Photoshop-style: About IS the splash panel. Its credit lines mirror
        // brand\CREDITS.md — Holly Watson (identity) and the Nullsoft/Geiss
        // engine attribution must ship in any distributed build — and its
        // version string comes from licium_version.h, same as the exe metadata.
        HostSplashShow(GetModuleHandleW(0), 1);
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

    // Splash up first: it paints synchronously here, sits topmost through
    // engine init, and holds until the user dismisses it (Continue/click/Esc);
    // the render loop animates the fade-out (HostSplashTick).
    HostSplashShow(hInstance, 0);

    // Hand the plugin a NULL Winamp window + our own hInstance. NULL window ->
    // all its IPC SendMessages are guarded and no-op; hInstance lets its
    // GetModuleFileNameW() find OUR exe and root the config/resource dirs here.
    if (!g_plugin.PluginPreInitialize(0, hInstance)) { g_plugin.PluginQuit(); return 1; }
    if (!g_plugin.PluginInitialize())                { g_plugin.PluginQuit(); return 1; }
    g_bFullyExited = false;

    // Init succeeded and the window/menu are up: stop auto-dismissing modal
    // dialogs so the menu's Open/About boxes work as real user interactions.
    g_suppressDialogs = false;

    // Swap the engine's MilkDrop-era F1 text for the Licium key reference.
    // (Init loaded its own from a resource; it's never freed, so repointing
    // the global is safe. The overlay renderer draws wide text when
    // g_szHelp_W is set.)
    g_szHelp   = (wchar_t*)LICIUM_HELP_TEXT;
    g_szHelp_W = 1;

    // Re-apply the maximized state from the last session (saved by
    // DXContext::SaveWindow; the engine itself only restores position/size,
    // so a maximized window used to come back normal-sized). The engine's own
    // WM_SIZE handling resizes the backbuffer, exactly as for a user-driven
    // maximize. Dev/snapshot runs stay windowed: the flag is only 1 if the
    // last session actually ended maximized.
    if (g_hostMainWnd && g_hostIniFile[0] &&
        GetPrivateProfileIntW(L"settings", L"bMainWndMaximized", 0, g_hostIniFile))
        ShowWindow(g_hostMainWnd, SW_MAXIMIZE);

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

        HostSplashTick();

        // PluginRender() returns nonzero to keep going, zero to stop.
        if (!g_plugin.PluginRender(waveL, waveR)) running = false;
    }

    HostSplashDestroy();
    WasapiAudioStop();
    HostSpoutShutdown();
    g_plugin.PluginQuit();
    g_bFullyExited = true;
    return (int)msg.wParam;
}
