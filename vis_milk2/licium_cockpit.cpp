// licium_cockpit.cpp — the docked tool strip. See licium_cockpit.h.
//
// Aesthetic brief (Gary, 2026-07-26): minimalist squares, "nearly invisible"
// embossing, an Adobe-like press. Read literally:
//
//   * The emboss is ONE pixel of light on the top/left and one of shadow on the
//     bottom/right, both at very low alpha. At rest you should read an edge,
//     not a bevel — if you can name it as embossing it is too strong.
//   * The press INVERTS that pair (shadow above, light below) and darkens the
//     face. Inverting the light direction is the whole illusion of a button
//     going in; darkening alone reads as a hover, not a push.
//   * Only the square under the cursor lifts. Everything else stays quiet, per
//     the mockup note that carries over from the browser: very dark, very
//     quiet, only the active control lights up.
//
// Same quad idiom as the browser panel (client-centered SIMPLEVERTEX strips,
// labels queued through m_text so they flush on top of the UI pass).

#ifdef MILKDROP_STANDALONE

#include "api.h"
#include "licium_cockpit.h"
#include "plugin.h"

#include <stdio.h>
#include <string.h>

extern CPlugin g_plugin;

// ================================ palette ====================================
// Sampled off the browser panel so the dock reads as the same piece of chrome:
// its background is that panel's 0xD8000000 and its inner edge that panel's
// 0xFF303030. Changing these is changing the browser's too — keep them in step.

#define LC_BG          0xD8000000   // dock background (85% black over canvas)
#define LC_EDGE        0xFF303030   // 2px rule down the inner side
#define LC_FACE        0xFF1A1A20   // square at rest
#define LC_FACE_HOVER  0xFF24242C   // under the cursor
#define LC_FACE_DOWN   0xFF141418   // held
#define LC_FACE_OFF    0xFF15151A   // unavailable right now
#define LC_EMB_LIGHT   0x18FFFFFF   // 9% white  - the "nearly invisible" part
#define LC_EMB_DARK    0x30000000   // 19% black
#define LC_LABEL       0xFF9A9AA6
#define LC_LABEL_HOVER 0xFFE6E6F0
#define LC_LABEL_OFF   0xFF4A4A52
#define LC_ACCENT      0xFF6E6EA0   // active tool's inner tick
#define LC_STATUS      0xFF8A8A96

// ================================ the tools ==================================
// Two to four characters, because the square is sized off the font and a longer
// label would either shrink the type or burst the square. The full name goes in
// the status line on hover, which is where discovery actually happens.

struct LcTool
{
    const wchar_t* glyph;
    const wchar_t* name;
    int            built;   // 0 = slot is real but the slice isn't written yet
};

static const LcTool g_tools[LCT_COUNT] =
{
    { L"Col",  L"Color - hue, saturation, light, contrast, cycle", 1 },
    { L"Mot",  L"Motion - smear, gain, smooth, band tilt",         0 },
    { L"Rct",  L"React - source to target links, response curves", 0 },
    { L"Zm",   L"Zoom - pull back past the frame",                 0 },
    { L"Rot",  L"Rotate - turn the view",                          0 },
    { L"Crv",  L"Curves - the shaping curves, per slot",           0 },
    { L"Pal",  L"Palette - cycle this roll's palette",             0 },
    { L"Grd",  L"Grade - like, pass, tags, error flag",            0 },
    { L"Fam",  L"Family - promote this roll and scan its grafts",  0 },
    { L"Sch",  L"Search - run a latent search",                    1 },
};

// ================================ state ======================================

static int     g_hover  = -1;
static int     g_down   = -1;   // button currently held (armed for a click)
static int     g_active = -1;   // tool whose dialog is up, for the accent tick
static wchar_t g_status[160] = L"";

// Layout, recomputed per frame from the canvas size. Cheap (no GDI: the metrics
// come from the font height, not from measuring strings — the per-frame
// GetDC/GetTextExtentPoint32W round trip is exactly the stall that had to be
// cached out of the browser panel).
struct LcLayout
{
    float x0, x1;       // dock span
    float side;         // square edge
    float top;          // first square's top
    float pitch;        // square edge + gap
    float titleY;       // baseline row for the header
    int   fh;
};

static void LcComputeLayout(int w, int h, LcLayout* L)
{
    L->fh = g_plugin.GetFontHeight(SIMPLE_FONT);
    if (L->fh < 8) L->fh = 8;

    // Five lines tall (Gary 2026-07-27: twice the original, which read as small
    // buttons rattling around in a wide column). The square drives the dock
    // width rather than the other way round, so the buttons always fill it.
    L->side = (float)(L->fh * 5);

    float pad = (float)(L->fh / 2);
    if (pad < 4.0f) pad = 4.0f;

    float width = L->side + pad * 2.0f;

    // Ceiling on a narrow canvas (the porthole square can be 512px). At a third
    // the dock stays a strip rather than becoming the composition; past that
    // the squares shrink instead of the canvas.
    float cap = (float)w * 0.33f;
    if (width > cap && cap > 16.0f)
    {
        width   = cap;
        L->side = width - pad * 2.0f;
    }

    L->x1    = (float)w;
    L->x0    = (float)w - width;
    L->pitch = L->side + (float)(L->fh / 2);
    if (L->pitch < L->side + 2.0f) L->pitch = L->side + 2.0f;

    // TOP ALIGNED, under a header (Gary: "give it a title like Tools ... align
    // top not middle"). A centered column drifts as the window resizes; anchored
    // to the header the tools are always in the same place.
    L->titleY = (float)L->fh;
    L->top    = L->titleY + (float)(L->fh * 2);

    // Short canvas: squeeze the pitch before letting the column run off the
    // bottom, and only then the squares.
    float stack = L->pitch * (float)LCT_COUNT - (L->pitch - L->side);
    float avail = (float)h - L->top - (float)(L->fh * 3);
    if (stack > avail && avail > (float)LCT_COUNT)
    {
        L->pitch = avail / (float)LCT_COUNT;
        if (L->side > L->pitch - 1.0f) L->side = L->pitch - 1.0f;
        if (L->side < 6.0f) L->side = 6.0f;
    }
}

static void LcButtonRect(const LcLayout* L, int i, float* x0, float* y0,
                         float* x1, float* y1)
{
    float cx = (L->x0 + L->x1) * 0.5f;
    *x0 = cx - L->side * 0.5f;
    *x1 = *x0 + L->side;
    *y0 = L->top + L->pitch * (float)i;
    *y1 = *y0 + L->side;
}

static int LcHit(int px, int py, int w, int h)
{
    LcLayout L;
    LcComputeLayout(w, h, &L);
    if ((float)px < L.x0)
        return -1;
    for (int i = 0; i < LCT_COUNT; i++)
    {
        float x0, y0, x1, y1;
        LcButtonRect(&L, i, &x0, &y0, &x1, &y1);
        if ((float)px >= x0 && (float)px < x1 && (float)py >= y0 && (float)py < y1)
            return i;
    }
    return -1;   // inside the dock but between buttons
}

// ================================ drawing ====================================

typedef struct { float x, y, z; DWORD Diffuse; } LcVertex;
#define LC_VERTEX_FORMAT (D3DFVF_XYZ | D3DFVF_DIFFUSE)

static void LcRect(IDirect3DDevice9* dev, int w, int h,
                   float x0, float y0, float x1, float y1, DWORD color)
{
    LcVertex v[4];
    for (int i = 0; i < 4; i++)
    {
        v[i].x = -w * 0.5f + ((i % 2) ? x1 : x0);
        v[i].y =  h * 0.5f - ((i / 2) ? y0 : y1);
        v[i].z = 0.0f;
        v[i].Diffuse = color;
    }
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(LcVertex));
}

static void LcText(const wchar_t* s, int x0, int y0, int x1, int y1,
                   DWORD color, DWORD flags)
{
    RECT r = { x0, y0, x1, y1 };
    g_plugin.m_text.DrawTextW(g_plugin.GetFont(SIMPLE_FONT), (wchar_t*)s, -1, &r,
                              DT_SINGLELINE | DT_NOPREFIX | flags,
                              color, false, 0xFF000000);
}

// One square. `state`: 0 rest, 1 hover, 2 held, 3 unavailable.
static void LcDrawButton(IDirect3DDevice9* dev, int w, int h, const LcLayout* L,
                         int i, int state)
{
    float x0, y0, x1, y1;
    LcButtonRect(L, i, &x0, &y0, &x1, &y1);

    DWORD face = LC_FACE;
    if      (state == 1) face = LC_FACE_HOVER;
    else if (state == 2) face = LC_FACE_DOWN;
    else if (state == 3) face = LC_FACE_OFF;
    LcRect(dev, w, h, x0, y0, x1, y1, face);

    // The emboss. Held flips which pair of edges catches the light, which is
    // what makes the square read as pushed in rather than merely darker.
    DWORD hi = (state == 2) ? LC_EMB_DARK  : LC_EMB_LIGHT;
    DWORD lo = (state == 2) ? LC_EMB_LIGHT : LC_EMB_DARK;
    LcRect(dev, w, h, x0, y0, x1, y0 + 1.0f, hi);          // top
    LcRect(dev, w, h, x0, y0, x0 + 1.0f, y1, hi);          // left
    LcRect(dev, w, h, x0, y1 - 1.0f, x1, y1, lo);          // bottom
    LcRect(dev, w, h, x1 - 1.0f, y0, x1, y1, lo);          // right

    // Active tool: a short tick against the inner edge. Deliberately not a
    // border or a fill — the brief is that only the hovered control lights up,
    // so "which dialog is open" has to whisper.
    if (i == g_active)
    {
        float m = (y1 - y0) * 0.28f;
        LcRect(dev, w, h, x0 - 3.0f, y0 + m, x0 - 1.0f, y1 - m, LC_ACCENT);
    }

    DWORD lab = LC_LABEL;
    if      (state == 3)              lab = LC_LABEL_OFF;
    else if (state == 1 || state == 2) lab = LC_LABEL_HOVER;

    // Center the glyph on the square: one line of text, vertically centered by
    // hand because DT_VCENTER is unreliable through the queued text manager.
    int ty = (int)((y0 + y1) * 0.5f) - L->fh / 2;
    LcText(g_tools[i].glyph, (int)x0, ty, (int)x1, ty + L->fh + 2, lab, DT_CENTER);
}

extern "C" void LiciumCockpitRender(IDirect3DDevice9* dev, int w, int h)
{
    if (!dev || w <= 0 || h <= 0)
        return;
    if (!g_plugin.GetFont(SIMPLE_FONT))
        return;

    // State block mirrors LatentBrowserRender / DrawDarkTranslucentBox exactly;
    // restored at the end so the engine's own passes are undisturbed.
    dev->SetVertexShader(NULL);
    dev->SetPixelShader(NULL);
    dev->SetFVF(LC_VERTEX_FORMAT);
    dev->SetTexture(0, NULL);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);

    LcLayout L;
    LcComputeLayout(w, h, &L);

    LcRect(dev, w, h, L.x0, 0.0f, L.x1, (float)h, LC_BG);
    LcRect(dev, w, h, L.x0, 0.0f, L.x0 + 2.0f, (float)h, LC_EDGE);

    // Header. A hairline under it does the work a box would, without adding a
    // second frame inside a strip that is already framed by its own edge.
    LcText(L"TOOLS", (int)L.x0, (int)L.titleY, (int)L.x1,
           (int)L.titleY + L.fh + 2, LC_LABEL, DT_CENTER);
    {
        float ry = L.titleY + (float)L.fh + 4.0f;
        float m  = (float)(L.fh / 2);
        LcRect(dev, w, h, L.x0 + m, ry, L.x1 - m, ry + 1.0f, LC_EMB_LIGHT);
    }

    for (int i = 0; i < LCT_COUNT; i++)
    {
        int state = 0;
        if (!g_tools[i].built)      state = 3;
        if (i == g_hover && g_down < 0 && g_tools[i].built) state = 1;
        if (i == g_down)            state = 2;
        LcDrawButton(dev, w, h, &L, i, state);
    }

    // Status line, under the column: the hovered tool's full name, or whatever
    // the last invoke had to say. This is where a 3-letter glyph becomes
    // discoverable, so it is worth the two lines of space.
    const wchar_t* msg = NULL;
    if (g_hover >= 0)          msg = g_tools[g_hover].name;
    else if (g_status[0])      msg = g_status;

    if (msg)
    {
        float sy = L.top + L.pitch * (float)LCT_COUNT + (float)L.fh;
        if (sy < (float)(h - L.fh * 2))
        {
            // Right-aligned into the canvas so a long name has room to run,
            // rather than clipping inside the dock's narrow column.
            int rx1 = (int)L.x0 - 6;
            int rx0 = rx1 - (w * 2) / 3;
            if (rx0 < 4) rx0 = 4;
            LcText(msg, rx0, (int)sy, rx1, (int)sy + L.fh + 2, LC_STATUS, DT_RIGHT);
        }
    }

    dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
}

// ================================ mouse ======================================

extern "C" int LiciumCockpitMouseMove(int x, int y, int w, int h)
{
    int hit = LcHit(x, y, w, h);
    g_hover = hit;
    LcLayout L;
    LcComputeLayout(w, h, &L);
    return ((float)x >= L.x0) ? 1 : 0;   // over the dock at all
}

extern "C" int LiciumCockpitMouseDown(int x, int y, int w, int h)
{
    int hit = LcHit(x, y, w, h);
    LcLayout L;
    LcComputeLayout(w, h, &L);
    if ((float)x < L.x0)
        return 0;
    // Consume clicks anywhere on the dock, buttons or not: the background of a
    // docked strip is not a place that should start dragging the window.
    g_down  = (hit >= 0 && g_tools[hit].built) ? hit : -1;
    g_hover = hit;
    return 1;
}

extern "C" int LiciumCockpitMouseUp(int x, int y, int w, int h)
{
    int armed = g_down;
    g_down = -1;
    if (armed < 0)
        return 0;
    // Fires only if the release lands on the same square that was pressed —
    // dragging off a button cancels it, as every toolbar since Motif has done.
    int hit = LcHit(x, y, w, h);
    if (hit == armed)
    {
        g_active = armed;
        LiciumCockpitInvoke(armed);
    }
    return 1;
}

extern "C" int LiciumCockpitWidth(int w, int h)
{
    LcLayout L;
    LcComputeLayout(w, h, &L);
    return (int)(L.x1 - L.x0);
}

extern "C" void LiciumCockpitSetStatus(const wchar_t* s)
{
    if (!s) { g_status[0] = 0; return; }
    wcsncpy(g_status, s, sizeof(g_status)/sizeof(g_status[0]) - 1);
    g_status[sizeof(g_status)/sizeof(g_status[0]) - 1] = 0;
}

#endif // MILKDROP_STANDALONE
