// licium_palettes.cpp — Palettes v1: multi-strategy roller + voting wall + pool.
//
// See licium_palettes.h for the contract and docs\PALETTES_DESIGN.md for the
// blessed design. Standalone-only. The carried-over laws and where they live:
//
//   * HSL LIES ABOUT BRIGHTNESS — OKLCH is the workhorse strategy (equal L/C =
//     equal perceived brightness/saturation across hues); HSL survives at 15%.
//   * VOTES BIND TO COLORS, NOT SEEDS — a roller change once orphaned 88 real
//     votes. Votes store quantized color lists, hash-matched, later-wins.
//   * LUMINANCE-SORTED RAMPS, deep -> accent; nothing reaches pure white.
//   * BLACK SEPARATORS on the wall — swatches judged against white wash out.
//   * NO HEAVY WORK PER VOTE — votes append to memory + file; nothing retrains
//     or recomputes on a keypress.

#ifdef MILKDROP_STANDALONE

#include "api.h"
#include "licium_palettes.h"
#include "plugin.h"
#include "licium_rng.h"
#include "licium_oklab.h"
#include "licium_palette_lib.h"

#include <stdio.h>
#include <math.h>

extern CPlugin g_plugin;

// ================================ the roller ==================================

static void HslToRgb(float h, float s, float l, float* out)
{
    h = h - floorf(h);
    float c = (1.0f - fabsf(2.0f*l - 1.0f)) * s;
    float hp = h * 6.0f;
    float x = c * (1.0f - fabsf(fmodf(hp, 2.0f) - 1.0f));
    float r = 0, g = 0, b = 0;
    if      (hp < 1) { r = c; g = x; }
    else if (hp < 2) { r = x; g = c; }
    else if (hp < 3) { g = c; b = x; }
    else if (hp < 4) { g = x; b = c; }
    else if (hp < 5) { r = x; b = c; }
    else             { r = c; b = x; }
    float m = l - c * 0.5f;
    out[0] = r + m; out[1] = g + m; out[2] = b + m;
}

// Scheme hue offsets (radians), shared by the OKLCH and HSL strategies:
// 0 analogous / 1 mono / 2 split-comp / 3 triad / 4 comp.
static float SchemeHue(int scheme, int k, int n, float h0, float spread)
{
    switch (scheme)
    {
    case 0:  return h0 + (k - (n - 1) * 0.5f) * spread;
    case 1:  return h0;
    case 2:  { const float off[3] = { 0.0f, 3.1416f - 0.52f, 3.1416f + 0.52f };
               return h0 + off[k % 3]; }
    case 3:  return h0 + k * 2.0944f;
    default: return h0 + (k % 2) * 3.1416f;
    }
}

// Strategy 0: harmony schemes in OKLCH — anchors of equal perceived chroma on
// an ascending perceived-lightness ladder. Gamut-clipped by chroma reduction.
static int RollOKLCH(unsigned short seed, float rgb[5][3])
{
    LiciumRng r; r.Seed(seed);
    int   scheme = r.I(0, 4);
    int   n      = r.I(3, 5);
    float h0     = r.F(0.0f, 6.2832f);
    float spread = r.F(0.30f, 0.55f);
    float C      = r.F(0.10f, 0.24f);
    for (int k = 0; k < n; k++)
    {
        float t  = (n > 1) ? k / (float)(n - 1) : 0.0f;
        float L  = 0.30f + 0.55f * t + r.F(-0.05f, 0.05f);
        float Ck = C * r.F(0.75f, 1.15f);
        if (L < 0.15f) L = 0.15f;
        if (L > 0.90f) L = 0.90f;   // no white, even at the accent stop
        LicOklchToSrgbClipped(L, Ck, SchemeHue(scheme, k, n, h0, spread),
                              &rgb[k][0], &rgb[k][1], &rgb[k][2]);
    }
    return n;
}

// Strategy 1: the curated library (848 palettes regenerated from public
// sources — see licium_palette_lib.h's header comment).
static int RollCurated(unsigned short seed, float rgb[5][3])
{
    LiciumRng r; r.Seed(seed);
    const LiciumLibPalette* p = &LICIUM_PAL_LIB[r.Next16() % LICIUM_PAL_LIB_COUNT];
    for (int k = 0; k < p->n; k++)
    {
        rgb[k][0] = ((p->c[k] >> 16) & 0xFF) / 255.0f;
        rgb[k][1] = ((p->c[k] >>  8) & 0xFF) / 255.0f;
        rgb[k][2] = ( p->c[k]        & 0xFF) / 255.0f;
    }
    return p->n;
}

// Strategy 2: IQ cosine ramps — a + b*cos(2pi(c*t + d)), jittered canonical
// sets + a whole-palette hue rotate (same d shift on all channels keeps the
// channels correlated — independent RGB drifts are the mud law).
static int RollCosine(unsigned short seed, float rgb[5][3])
{
    static const float CS[5][4][3] = {
        { {0.50f,0.50f,0.50f}, {0.50f,0.50f,0.50f}, {1,1,1},          {0.00f,0.33f,0.67f} },
        { {0.50f,0.50f,0.50f}, {0.50f,0.50f,0.50f}, {1,1,1},          {0.00f,0.10f,0.20f} },
        { {0.50f,0.50f,0.50f}, {0.50f,0.50f,0.50f}, {1,1,0.5f},       {0.80f,0.90f,0.30f} },
        { {0.50f,0.50f,0.50f}, {0.50f,0.50f,0.50f}, {2,1,0},          {0.50f,0.20f,0.25f} },
        { {0.80f,0.50f,0.40f}, {0.20f,0.40f,0.20f}, {2,1,1},          {0.00f,0.25f,0.25f} },
    };
    LiciumRng r; r.Seed(seed);
    int   set = r.I(0, 4);
    float rot = r.F(0.0f, 1.0f);
    float a[3], b[3], c[3], d[3];
    for (int j = 0; j < 3; j++)
    {
        a[j] = CS[set][0][j] + r.F(-0.05f, 0.05f);
        b[j] = CS[set][1][j] + r.F(-0.05f, 0.05f);
        c[j] = CS[set][2][j];
        d[j] = CS[set][3][j] + rot + r.F(-0.04f, 0.04f);
        if (b[j] > 0.97f - a[j]) b[j] = 0.97f - a[j];   // peak <= 0.97: no white
        if (b[j] > a[j])         b[j] = a[j];           // floor >= 0
        if (b[j] < 0.0f)         b[j] = 0.0f;
    }
    for (int k = 0; k < 5; k++)
    {
        float t = k / 4.0f;
        for (int j = 0; j < 3; j++)
        {
            float v = a[j] + b[j] * cosf(6.2832f * (c[j] * t + d[j]));
            rgb[k][j] = (v < 0) ? 0 : (v > 1) ? 1 : v;
        }
    }
    return 5;
}

// Strategy 3: legacy HSL harmony, kept small (15%) for variety. Luminance
// ladder climbs to bright accents (~0.86) — the old dim-wall fix.
static int RollHSL(unsigned short seed, float rgb[5][3])
{
    LiciumRng r; r.Seed(seed);
    int   scheme = r.I(0, 4);
    int   n      = r.I(3, 5);
    float h0     = r.F(0.0f, 1.0f);
    float spread = r.F(0.08f, 0.15f);
    float S      = r.F(0.55f, 0.90f);
    for (int k = 0; k < n; k++)
    {
        float t = (n > 1) ? k / (float)(n - 1) : 0.0f;
        float L = 0.25f + 0.61f * t + r.F(-0.04f, 0.04f);
        float h = SchemeHue(scheme, k, n, h0 * 6.2832f, spread * 6.2832f) / 6.2832f;
        HslToRgb(h, S * r.F(0.85f, 1.10f), L, rgb[k]);
    }
    return n;
}

extern "C" int PaletteRoll(unsigned short seed, float rgb[5][3], int* strategyOut)
{
    // Strategy dispatch by a seed-hash INDEPENDENT of the strategies' LCG
    // streams (each strategy re-seeds its own), weights per the blessed doc:
    // oklch 32 / curated 30 / cosine 23 / hsl 15.
    unsigned int pct = (((unsigned int)seed * 2654435761u) >> 16) % 100u;
    int strat, n;
    if      (pct < 32) { strat = 0; n = RollOKLCH  (seed, rgb); }
    else if (pct < 62) { strat = 1; n = RollCurated(seed, rgb); }
    else if (pct < 85) { strat = 2; n = RollCosine (seed, rgb); }
    else               { strat = 3; n = RollHSL    (seed, rgb); }
    if (strategyOut) *strategyOut = strat;

    // Deep -> accent, always (insertion sort by perceived luminance).
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && LicSrgbLum(rgb[j]) < LicSrgbLum(rgb[j-1]); j--)
        {
            float tmp[3] = { rgb[j][0], rgb[j][1], rgb[j][2] };
            rgb[j][0] = rgb[j-1][0]; rgb[j][1] = rgb[j-1][1]; rgb[j][2] = rgb[j-1][2];
            rgb[j-1][0] = tmp[0]; rgb[j-1][1] = tmp[1]; rgb[j-1][2] = tmp[2];
        }
    return n;
}

// ================================ votes ======================================
// One vote = one quantized color list + a verdict, bound to the COLORS.
// File line: "#RRGGBB,#RRGGBB,... <1|0|-1>"  (1 like, 0 pass, -1 cleared),
// later-wins on re-vote. Nothing is ever deleted from the file.

struct PalVote
{
    unsigned int  hash;
    int           vote;
    unsigned char n;
    unsigned char rgb[5][3];
};

static PalVote* g_votes     = NULL;
static int      g_voteCount = 0, g_voteCap = 0;
static int      g_votesLoaded = 0;
static wchar_t  g_votesPath[MAX_PATH] = {0};

static void QuantizeKey(const float rgb[5][3], int n, unsigned char q[5][3], char* key /*>=48 chars*/)
{
    char* p = key;
    for (int k = 0; k < n; k++)
    {
        for (int j = 0; j < 3; j++)
        {
            float v = rgb[k][j];
            q[k][j] = (unsigned char)((v < 0 ? 0 : v > 1 ? 1 : v) * 255.0f + 0.5f);
        }
        p += sprintf(p, "%s#%02X%02X%02X", k ? "," : "", q[k][0], q[k][1], q[k][2]);
    }
    *p = 0;
}

static unsigned int Fnv1a(const char* s)
{
    unsigned int h = 2166136261u;
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h;
}

static PalVote* FindVote(unsigned int hash)
{
    for (int i = 0; i < g_voteCount; i++)
        if (g_votes[i].hash == hash) return &g_votes[i];
    return NULL;
}

static PalVote* UpsertVote(unsigned int hash, const unsigned char q[5][3], int n, int vote)
{
    PalVote* v = FindVote(hash);
    if (!v)
    {
        if (g_voteCount == g_voteCap)
        {
            g_voteCap = g_voteCap ? g_voteCap * 2 : 64;
            PalVote* nv = (PalVote*)realloc(g_votes, g_voteCap * sizeof(PalVote));
            if (!nv) return NULL;
            g_votes = nv;
        }
        v = &g_votes[g_voteCount++];
        v->hash = hash;
        v->n = (unsigned char)n;
        memcpy(v->rgb, q, sizeof(v->rgb));
    }
    v->vote = vote;
    return v;
}

extern "C" const wchar_t* PaletteGetVotesPath(void)
{
    if (!g_votesPath[0])
        swprintf(g_votesPath, L"%spalette_votes.txt", g_plugin.m_szMilkdrop2Path);
    return g_votesPath;
}

static void LoadVotes()
{
    if (g_votesLoaded) return;
    g_votesLoaded = 1;
    FILE* f = _wfopen(PaletteGetVotesPath(), L"rb");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f))
    {
        char keybuf[256];
        int vote;
        if (sscanf(line, "%255s %d", keybuf, &vote) != 2) continue;
        unsigned char q[5][3] = {0};
        int n = 0;
        const char* p = keybuf;
        while (*p == '#' && n < 5)
        {
            unsigned int r, g, b;
            if (sscanf(p + 1, "%2x%2x%2x", &r, &g, &b) != 3) break;
            q[n][0] = (unsigned char)r; q[n][1] = (unsigned char)g; q[n][2] = (unsigned char)b;
            n++;
            p += 8;                    // "#RRGGBB" + separator
            if (*p == 0 || p[-1] != ',') break;
        }
        if (n >= 2)
            UpsertVote(Fnv1a(keybuf), q, n, vote);   // later lines win
    }
    fclose(f);
}

static void CastVote(unsigned short seed, int vote)
{
    float rgb[5][3];
    int n = PaletteRoll(seed, rgb, NULL);
    unsigned char q[5][3] = {0};
    char key[64];
    QuantizeKey(rgb, n, q, key);
    LoadVotes();
    if (!UpsertVote(Fnv1a(key), q, n, vote)) return;
    FILE* f = _wfopen(PaletteGetVotesPath(), L"ab");
    if (f) { fprintf(f, "%s %d\r\n", key, vote); fclose(f); }
}

// Vote (or 999 = none) for the palette a seed currently rolls to.
static int VoteForSeed(unsigned short seed)
{
    float rgb[5][3];
    int n = PaletteRoll(seed, rgb, NULL);
    unsigned char q[5][3] = {0};
    char key[64];
    QuantizeKey(rgb, n, q, key);
    LoadVotes();
    PalVote* v = FindVote(Fnv1a(key));
    return v ? v->vote : 999;
}

extern "C" int PalettePoolCount(void)
{
    LoadVotes();
    int c = 0;
    for (int i = 0; i < g_voteCount; i++)
        if (g_votes[i].vote == 1) c++;
    return c;
}

extern "C" int PalettePoolGet(int idx, float rgb[5][3])
{
    LoadVotes();
    int c = 0;
    for (int i = 0; i < g_voteCount; i++)
        if (g_votes[i].vote == 1)
        {
            if (c == idx)
            {
                for (int k = 0; k < g_votes[i].n; k++)
                    for (int j = 0; j < 3; j++)
                        rgb[k][j] = g_votes[i].rgb[k][j] / 255.0f;
                return g_votes[i].n;
            }
            c++;
        }
    return 0;
}

// ================================ the wall ===================================

#define WALL_COLS  6
#define WALL_ROWS  5
#define WALL_CELLS (WALL_COLS * WALL_ROWS)
#define WALL_PAGES ((65536 + WALL_CELLS - 1) / WALL_CELLS)   // seeds wrap the 16-bit space

static int g_wallOpen = 0;
static int g_wallPage = 0;
static int g_wallCur  = 0;   // 0..29 within the page

static unsigned short CellSeed(int page, int cell)
{
    return (unsigned short)((page * WALL_CELLS + cell) & 0xFFFF);
}

extern "C" void PaletteWallOpen(void)   { LoadVotes(); g_wallOpen = 1; }
extern "C" void PaletteWallClose(void)  { g_wallOpen = 0; }
extern "C" int  PaletteWallIsOpen(void) { return g_wallOpen; }

extern "C" int PaletteWallHandleChar(int ch)
{
    if (!g_wallOpen) return 0;
    unsigned short seed = CellSeed(g_wallPage, g_wallCur);
    switch (ch)
    {
    case 'y': case 'Y': case ' ':
        CastVote(seed, 1);
        // liking flows the cursor forward — voting a page is one hand resting
        // on Y/Space + arrows, not vote-move-vote-move
        if (g_wallCur < WALL_CELLS - 1) g_wallCur++;
        return 1;
    case 'n': case 'N': case 'x': case 'X':
        CastVote(seed, 0);
        if (g_wallCur < WALL_CELLS - 1) g_wallCur++;
        return 1;
    case 'u': case 'U':
        CastVote(seed, -1);
        return 1;
    case 'v': case 'V':
        PaletteWallClose();
        return 1;
    default:
        return 1;   // the wall eats every char while open
    }
}

extern "C" int PaletteWallHandleKeydown(int vk)
{
    if (!g_wallOpen) return 0;
    switch (vk)
    {
    case VK_LEFT:   if (g_wallCur % WALL_COLS)                  g_wallCur--; return 1;
    case VK_RIGHT:  if (g_wallCur % WALL_COLS < WALL_COLS - 1)  g_wallCur++; return 1;
    case VK_UP:     if (g_wallCur >= WALL_COLS)                 g_wallCur -= WALL_COLS; return 1;
    case VK_DOWN:   if (g_wallCur < WALL_CELLS - WALL_COLS)     g_wallCur += WALL_COLS; return 1;
    case VK_PRIOR:  g_wallPage = (g_wallPage + WALL_PAGES - 1) % WALL_PAGES; return 1;
    case VK_NEXT:   g_wallPage = (g_wallPage + 1) % WALL_PAGES; return 1;
    case VK_ESCAPE: PaletteWallClose(); return 1;
    default:
        return 1;   // modal: swallow every keydown (incl. Space's keydown half)
    }
}

// ---- drawing ----------------------------------------------------------------
// Same quad idiom as the engine's own DrawDarkTranslucentBox: SIMPLEVERTEX
// (XYZ|DIFFUSE) quads in client-centered coordinates, DrawPrimitiveUP.

typedef struct { float x, y, z; DWORD Diffuse; } WallVertex;
#define WALL_VERTEX_FORMAT (D3DFVF_XYZ | D3DFVF_DIFFUSE)

static void WallRect(IDirect3DDevice9* dev, int w, int h,
                     float x0, float y0, float x1, float y1, DWORD color)
{
    WallVertex v[4];
    for (int i = 0; i < 4; i++)
    {
        v[i].x = -w * 0.5f + ((i % 2) ? x1 : x0);
        v[i].y =  h * 0.5f - ((i / 2) ? y0 : y1);
        v[i].z = 0.0f;
        v[i].Diffuse = color;
    }
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(WallVertex));
}

extern "C" void PaletteWallRender(IDirect3DDevice9* dev, int w, int h)
{
    if (!g_wallOpen || !dev || w <= 0 || h <= 0) return;

    // State block mirrors DrawDarkTranslucentBox (restore at the end).
    dev->SetVertexShader(NULL);
    dev->SetPixelShader(NULL);
    dev->SetFVF(WALL_VERTEX_FORMAT);
    dev->SetTexture(0, NULL);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);

    // Near-black backdrop (the law: palettes are judged against BLACK).
    WallRect(dev, w, h, 0, 0, (float)w, (float)h, 0xF2000000);

    float headerH = h * 0.075f;
    float cellW = w / (float)WALL_COLS;
    float cellH = (h - headerH) / (float)WALL_ROWS;
    float pad   = (cellW < cellH ? cellW : cellH) * 0.06f;   // the black separator

    for (int cell = 0; cell < WALL_CELLS; cell++)
    {
        float rgb[5][3];
        int n = PaletteRoll(CellSeed(g_wallPage, cell), rgb, NULL);

        float cx0 = (cell % WALL_COLS) * cellW + pad;
        float cy0 = headerH + (cell / WALL_COLS) * cellH + pad;
        float cx1 = cx0 + cellW - 2 * pad;
        float cy1 = cy0 + cellH - 2 * pad;

        // vertical bands, deep -> accent left to right
        float bandW = (cx1 - cx0) / n;
        for (int k = 0; k < n; k++)
        {
            DWORD col = 0xFF000000
                | ((DWORD)(rgb[k][0] * 255.0f + 0.5f) << 16)
                | ((DWORD)(rgb[k][1] * 255.0f + 0.5f) << 8)
                |  (DWORD)(rgb[k][2] * 255.0f + 0.5f);
            WallRect(dev, w, h, cx0 + k * bandW, cy0, cx0 + (k + 1) * bandW, cy1 - 6.0f, col);
        }

        // vote badge: a bar under the swatch (green like / dark-red pass)
        int vote = VoteForSeed(CellSeed(g_wallPage, cell));
        if (vote == 1)      WallRect(dev, w, h, cx0, cy1 - 4.0f, cx1, cy1, 0xFF30C060);
        else if (vote == 0) WallRect(dev, w, h, cx0, cy1 - 4.0f, cx1, cy1, 0xFF702020);

        // cursor: light (not white) outline
        if (cell == g_wallCur)
        {
            DWORD oc = 0xFFB8B8B8;
            float t = 2.0f;
            WallRect(dev, w, h, cx0 - t, cy0 - t, cx1 + t, cy0,     oc);
            WallRect(dev, w, h, cx0 - t, cy1,     cx1 + t, cy1 + t, oc);
            WallRect(dev, w, h, cx0 - t, cy0,     cx0,     cy1,     oc);
            WallRect(dev, w, h, cx1,     cy0,     cx1 + t, cy1,     oc);
        }
    }

    dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);

    // Header labels queue through the engine's text manager (raw DrawText
    // renders nothing) and flush after the UI pass, landing on top.
    {
        wchar_t buf[192];
        int strat;
        float rgb[5][3];
        PaletteRoll(CellSeed(g_wallPage, g_wallCur), rgb, &strat);
        static const wchar_t* stratName[4] = { L"oklch", L"curated", L"cosine", L"hsl" };
        swprintf(buf, L"PALETTES   P%d-%04X (%s)   page %d   |   Y/Space like   N pass   U clear   PgUp/PgDn page   V close",
                 PALETTE_GENVER, CellSeed(g_wallPage, g_wallCur), stratName[strat], g_wallPage + 1);
        RECT r = { 12, 6, w - 12, (int)headerH };
        g_plugin.m_text.DrawTextW(g_plugin.GetFont(SIMPLE_FONT), buf, -1, &r,
                                  DT_SINGLELINE | DT_NOPREFIX | DT_WORD_ELLIPSIS,
                                  0xFFCCCCCC, false, 0xFF000000);
    }
}

#endif // MILKDROP_STANDALONE
