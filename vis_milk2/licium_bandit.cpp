// licium_bandit.cpp — Bandit v1, the shader slot machine.
//
// See licium_bandit.h for the contract and docs\BANDIT_DESIGN.md for the design.
// Standalone-only (MILKDROP_STANDALONE); the Winamp DLL never compiles this.
//
// The three behavior laws carried over from the old (closed) tree — knowledge,
// not code — and where they live here:
//
//   * EQUILIBRIUM. Anything repeatedly added into a feedback surface converges to
//     gain/(1-decay). Left unscaled, a "reasonable-looking" ink gain blows out to
//     25-125x and every roll is a white plate. So the injection gain is drawn as a
//     multiple OF (1-decay): k in [0.5,1.5] means the plume settles at k, whatever
//     the decay. The clamp that backs it up is hue-preserving VECTOR scaling
//     (scale the whole rgb by one factor) — per-channel min() clips channels
//     independently, which drags saturated color toward white.
//   * AUDIO GUARANTEE. Every roll must dance: injection is bass-driven and the
//     comp light direction is mid-driven, so no roll can come out deaf.
//   * PALETTE DISCIPLINE. Colors come from ONE luminance-sorted ramp (deep -> mid
//     -> accent) built from a single rolled hue scheme. Independent RGB rolls
//     average to gray mud. Nothing reaches pure white.

#ifdef MILKDROP_STANDALONE

#include "api.h"
#include "licium_bandit.h"
#include "plugin.h"

#include <stdio.h>
#include <math.h>
#include <shlobj.h>

extern CPlugin g_plugin;

// ---- deterministic RNG ------------------------------------------------------
// The fixed-spec LCG lives in licium_rng.h, shared by all Licium tools (each
// tool seeds its own instance; streams never mix). Bandit's ID contract rides
// on that spec — see the header's warning before touching it.
#include "licium_rng.h"
typedef LiciumRng BanditRng;

// ---- rolled knobs -----------------------------------------------------------
// The draw ORDER below is the contract (a reordered draw = a different preset for
// the same ID = a broken ID). Add new knobs only at the END, and only with a
// GENVER bump.

struct BanditKnobs
{
    float decay;            // trail persistence in buf0
    float curl;             // swirl strength of the advection field
    float fieldScale;       // spatial frequency of the field
    float fieldSpeed;       // how fast the field evolves
    float advect;           // how hard dye is pushed along the field
    float injectK;          // equilibrium multiple: plume settles at ~injectK
    float injectAudio;      // share of injection driven by bass (audio guarantee)
    float srcX, srcY;       // injection source home position
    float srcRX, srcRY;     // how far it wanders
    float srcSX, srcSY;     // how fast it wanders
    float srcPhase;         // phase offset, so x/y motion isn't locked in step
    float srcSharp;         // source tightness (small dot .. soft cloud)
    int   iters;            // buf0 intra-frame ping-pong count
    float pal[3][3];        // luminance-sorted ramp: deep, mid, accent
    float ink[3];           // injected ink color (accent-leaning)
    float sheen;            // comp gradient-highlight power
    float gradGain;         // how much buf0's gradient tilts the surface normal
    float densGain;         // how fast density climbs the palette ramp
    int   arche;            // velocity-field archetype (see EmitPreset)
};

// HSV -> RGB with an explicit value/saturation ladder, so the ramp is built from
// ONE hue scheme instead of three independent color rolls (palette discipline).
static void HsvToRgb(float h, float s, float v, float* out)
{
    h = h - floorf(h);
    float i = floorf(h * 6.0f);
    float f = h * 6.0f - i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - f * s);
    float t = v * (1.0f - (1.0f - f) * s);
    switch (((int)i) % 6)
    {
    case 0: out[0]=v; out[1]=t; out[2]=p; break;
    case 1: out[0]=q; out[1]=v; out[2]=p; break;
    case 2: out[0]=p; out[1]=v; out[2]=t; break;
    case 3: out[0]=p; out[1]=q; out[2]=v; break;
    case 4: out[0]=t; out[1]=p; out[2]=v; break;
    default:out[0]=v; out[1]=p; out[2]=q; break;
    }
}

static void RollKnobs(unsigned short seed, BanditKnobs* k)
{
    BanditRng rng;
    rng.Seed(seed);

    // Decay is PER FRAME, so the useful range is narrow and high: 0.96 is already
    // a ~17-frame half-life (~0.3s at 60fps), 0.992 is ~85 frames (~1.4s). Below
    // ~0.96 dye dies as fast as it is laid down and every roll is a thin comet
    // instead of a plume — the first tuning pass learned that the hard way. This
    // range is also what the remembered equilibrium ratio (~25-125x) implies:
    // 1/(1-decay) over this range is 25..125.
    k->decay       = rng.F(0.960f, 0.992f);
    k->curl        = rng.F(0.6f,  2.4f);
    k->fieldScale  = rng.F(1.8f,  5.5f);
    k->fieldSpeed  = rng.F(0.05f, 0.35f);
    k->advect      = rng.F(0.8f,  2.4f);

    // Equilibrium law: gain is a multiple of (1-decay), applied at emit time.
    k->injectK     = rng.F(0.5f,  1.5f);
    k->injectAudio = rng.F(0.35f, 0.75f);   // fraction of the gain that is bass

    // Source placement + motion. The old generator pinned the source low-centre
    // and every roll came out a rising plume; rolling the home position and both
    // wander axes is what buys variety here.
    k->srcX        = rng.F(0.25f, 0.75f);
    k->srcY        = rng.F(0.25f, 0.75f);
    k->srcRX       = rng.F(0.0f,  0.28f);
    k->srcRY       = rng.F(0.0f,  0.28f);
    k->srcSX       = rng.F(0.12f, 0.85f);
    k->srcSY       = rng.F(0.12f, 0.85f);
    k->srcPhase    = rng.F(0.0f,  6.283f);
    // Source tightness. exp(-d*d*sharp) at 1400 is a ~3%-radius pinprick, which
    // advects into a hairline; 60..420 is a soft mouth (~13%..5% radius) that
    // actually feeds a plume.
    k->srcSharp    = rng.F(60.0f, 420.0f);

    k->iters       = rng.I(1, 3);

    // Palette: one hue base + a spread, three stops climbing in luminance.
    // Value ladder tops out at 0.92 — no pure white, ever.
    float hue      = rng.F(0.0f, 1.0f);
    float spread   = rng.F(0.04f, 0.22f) * (rng.Next16() & 1 ? 1.0f : -1.0f);
    float sat      = rng.F(0.55f, 0.95f);
    HsvToRgb(hue,              sat,         rng.F(0.06f, 0.16f), k->pal[0]);  // deep
    HsvToRgb(hue + spread,     sat * 0.85f, rng.F(0.35f, 0.55f), k->pal[1]);  // mid
    HsvToRgb(hue + spread*2.f, sat * 0.55f, rng.F(0.80f, 0.92f), k->pal[2]);  // accent

    // Ink leans on the accent end so fresh injection reads as the bright stop.
    HsvToRgb(hue + spread*1.5f, sat * 0.7f, rng.F(0.70f, 0.90f), k->ink);

    k->sheen       = rng.F(1.0f, 4.0f);
    k->gradGain    = rng.F(1.5f, 6.0f);
    // The medium's luminance sits well below 1 even at equilibrium (the ink is a
    // mid-luminance color scaled by ~injectK), so a gain near 1 leaves dens ~0.3
    // and the ramp never climbs off its dark deep stop — the whole roll reads as
    // a black plate with a smudge. Push it so plume cores reach the accent.
    k->densGain    = rng.F(2.0f, 4.5f);

    // Velocity-field archetype — appended at the END of the draw order (the
    // order above is the ID contract). One fixed field formula made every roll
    // the same comma-spiral in a different color (a 12-seed sweep confirmed it);
    // the archetype roll is where the composition variety comes from.
    k->arche       = rng.I(0, 3);
}

// ---- preset emission --------------------------------------------------------
// Emitted as bytes (ASCII + CRLF) rather than through a text stream, so the same
// seed produces a byte-identical file — the doc's acceptance test.

struct BanditText
{
    char*  buf;
    size_t len, cap;

    void Init()  { cap = 16384; len = 0; buf = (char*)malloc(cap); if (buf) buf[0] = 0; }
    void Free()  { free(buf); buf = NULL; }
    void Add(const char* fmt, ...)
    {
        if (!buf) return;
        char line[1024];
        va_list ap;
        va_start(ap, fmt);
        int n = _vsnprintf(line, sizeof(line) - 3, fmt, ap);
        va_end(ap);
        if (n < 0) n = (int)strlen(line);
        line[n] = 0;
        strcat(line, "\r\n");
        n += 2;
        if (len + n + 1 > cap)
        {
            cap *= 2;
            char* nb = (char*)realloc(buf, cap);
            if (!nb) return;
            buf = nb;
        }
        memcpy(buf + len, line, n + 1);
        len += n;
    }
};

// Emit a shader block as the engine's per-line "<prefix>_<n>=`<text>" format.
static void EmitShaderLines(BanditText* t, const char* prefix, const char** lines, int count)
{
    for (int i = 0; i < count; i++)
        t->Add("%s_%d=`%s", prefix, i + 1, lines[i]);
}

static void EmitPreset(unsigned short seed, const BanditKnobs* k, BanditText* t)
{
    // Equilibrium law, applied here rather than in the rolled math: the ink gain
    // a roll gets is its k-multiple SCALED BY (1-decay).
    const float oneMinusDecay = 1.0f - k->decay;
    const float injTotal      = k->injectK * oneMinusDecay;
    const float injBase       = injTotal * (1.0f - k->injectAudio);
    const float injBass       = injTotal * k->injectAudio;

    // Ceiling for the hue-preserving clamp: a touch above the accent stop, so the
    // medium can reach the top of its ramp but never bleach past it.
    float palMax = k->pal[2][0];
    if (k->pal[2][1] > palMax) palMax = k->pal[2][1];
    if (k->pal[2][2] > palMax) palMax = k->pal[2][2];
    const float clampMax = palMax * 1.15f;

    t->Add("MILKDROP_PRESET_VERSION=201");
    t->Add("PSVERSION=3");
    t->Add("PSVERSION_WARP=3");
    t->Add("PSVERSION_COMP=3");
    t->Add("[preset00]");
    t->Add("fRating=3.000");
    t->Add("fGammaAdj=1.000");
    t->Add("fDecay=0.980");
    t->Add("nWaveMode=0");
    t->Add("fWaveAlpha=0.000");   // no drawn waveform: the medium IS the visual
    t->Add("bDarkenCenter=0");
    t->Add("zoom=1.00000");
    t->Add("rot=0.00000");
    t->Add("warp=0.00000");
    t->Add("buf0_iterations=%d", k->iters);

    // --- buf0: the fluid medium ---------------------------------------------
    // Curl of an analytic potential gives a divergence-free (i.e. fluid-looking,
    // never sourcing or sinking) velocity field, cheaply and without a solver.
    // Dye is advected by sampling BACKWARD along that field (semi-Lagrangian),
    // then decayed, then fresh ink is injected at the moving source.
    char L[24][512];
    const char* lines[24];
    int n = 0;
    #define ADDL(...) do { _snprintf(L[n], sizeof(L[n]) - 1, __VA_ARGS__); L[n][sizeof(L[n])-1]=0; lines[n]=L[n]; n++; } while (0)

    ADDL("shader_body");
    ADDL("{");
    ADDL("    float2 p = (uv - 0.5) * float2(texsize.x*texsize.w, 1.0);");
    ADDL("    float t = time * %.5f;", k->fieldSpeed);
    ADDL("    float a = %.5f;", k->fieldScale);
    // The velocity is the curl of an analytic potential phi (vel = (dphi/dy,
    // -dphi/dx)), so every archetype is divergence-free — fluid that neither
    // sources nor sinks. Four archetypes, four compositions:
    switch (k->arche)
    {
    default:
    case 0:
        // SWIRL — one dominant rotation cell (the original v1 field).
        // phi = sin(a*px + t)*cos(0.8a*py - 0.7t) + 0.5*sin(0.6a*(px+py) + 1.3t)
        ADDL("    float s1 = sin(p.x*a + t), c1 = cos(p.x*a + t);");
        ADDL("    float s2 = sin(p.y*a*0.8 - t*0.7), c2 = cos(p.y*a*0.8 - t*0.7);");
        ADDL("    float c3 = cos((p.x + p.y)*a*0.6 + t*1.3);");
        ADDL("    float dphidx = a*c1*c2 + 0.3*a*c3;");
        ADDL("    float dphidy = -a*0.8*s1*s2 + 0.3*a*c3;");
        break;
    case 1:
        // LATTICE — a checkerboard of counter-rotating cells; dye weaves
        // between them instead of winding one spiral.
        // phi = sin(a*px + t) * sin(a*py - 0.8t)
        ADDL("    float dphidx = a*cos(p.x*a + t)*sin(p.y*a - t*0.8);");
        ADDL("    float dphidy = a*sin(p.x*a + t)*cos(p.y*a - t*0.8);");
        break;
    case 2:
        // STREAM — a meandering horizontal jet with gentle vertical wander.
        // phi = 1.2*cos(1.5a*py - t) + 0.4*cos(0.5a*px + 0.8t)
        ADDL("    float dphidx = -0.2*a*sin(p.x*a*0.5 + t*0.8);");
        ADDL("    float dphidy = -1.8*a*sin(p.y*a*1.5 - t);");
        break;
    case 3:
        // DRIFT-WEAVE — the cell lattice plus a steady linear drift (the
        // x-linear potential term), so dye rains through the cells.
        // phi = 0.6*sin(a*px + t)*sin(a*py - 0.8t) + 1.1*px
        ADDL("    float dphidx = 0.6*a*cos(p.x*a + t)*sin(p.y*a - t*0.8) + 1.1;");
        ADDL("    float dphidy = 0.6*a*sin(p.x*a + t)*cos(p.y*a - t*0.8);");
        break;
    }
    ADDL("    float2 vel = float2(dphidy, -dphidx) * %.5f;", k->curl);
    ADDL("    float2 suv = uv - vel * %.5f * texsize.zw;", k->advect * 6.0f);
    ADDL("    float3 prev = tex2D(sampler_buf0, suv).xyz;");
    ADDL("    float2 src = float2(%.5f + %.5f*cos(time*%.5f + %.5f), %.5f + %.5f*sin(time*%.5f));",
         k->srcX, k->srcRX, k->srcSX, k->srcPhase, k->srcY, k->srcRY, k->srcSY);
    ADDL("    float d = length((uv - src) * float2(texsize.x*texsize.w, 1.0));");
    ADDL("    float ink = exp(-d*d*%.2f) * (%.6f + %.6f*bass_att);", k->srcSharp, injBase, injBass);
    ADDL("    float3 acc = prev*%.5f + float3(%.4f, %.4f, %.4f)*ink;",
         k->decay, k->ink[0], k->ink[1], k->ink[2]);
    ADDL("    float m = max(acc.x, max(acc.y, acc.z));");
    ADDL("    acc *= min(1.0, %.5f / (m + 0.0001));", clampMax);
    ADDL("    ret = acc;");
    ADDL("}");
    EmitShaderLines(t, "buf0", lines, n);

    // --- warp: near-passthrough ---------------------------------------------
    // The medium lives in buf0; the main surface is just a carrier here.
    n = 0;
    ADDL("shader_body");
    ADDL("{");
    ADDL("    ret = tex2D(sampler_main, uv).xyz * 0.0;");
    ADDL("}");
    EmitShaderLines(t, "warp", lines, n);

    // --- comp: shade the medium through the ramp -----------------------------
    // Treat buf0's luminance as a height field: its gradient tilts a normal, and
    // a light (whose direction is mid-driven — audio guarantee) puts a sheen on
    // the folds. Density then climbs the luminance-sorted ramp deep -> accent.
    n = 0;
    ADDL("shader_body");
    ADDL("{");
    ADDL("    float2 d = texsize_buf0.zw;");
    ADDL("    float3 c = tex2D(sampler_buf0, uv).xyz;");
    ADDL("    float lx = lum(tex2D(sampler_buf0, uv + float2(d.x,0)).xyz) - lum(tex2D(sampler_buf0, uv - float2(d.x,0)).xyz);");
    ADDL("    float ly = lum(tex2D(sampler_buf0, uv + float2(0,d.y)).xyz) - lum(tex2D(sampler_buf0, uv - float2(0,d.y)).xyz);");
    ADDL("    float3 nrm = normalize(float3(-lx*%.4f, -ly*%.4f, 1.0));", k->gradGain, k->gradGain);
    ADDL("    float3 lgt = normalize(float3(cos(time*0.23), sin(time*0.19), 0.55 + 0.45*mid_att));");
    ADDL("    float sheen = pow(saturate(dot(nrm, lgt)), %.4f);", k->sheen);
    ADDL("    float dens = saturate(lum(c) * %.4f);", k->densGain);
    ADDL("    float3 ramp = lerp(float3(%.4f,%.4f,%.4f), float3(%.4f,%.4f,%.4f), saturate(dens*2.0));",
         k->pal[0][0], k->pal[0][1], k->pal[0][2], k->pal[1][0], k->pal[1][1], k->pal[1][2]);
    ADDL("    ramp = lerp(ramp, float3(%.4f,%.4f,%.4f), saturate(dens*2.0 - 1.0));",
         k->pal[2][0], k->pal[2][1], k->pal[2][2]);
    ADDL("    ret = ramp + float3(%.4f,%.4f,%.4f) * sheen * dens * 0.6;",
         k->pal[2][0], k->pal[2][1], k->pal[2][2]);
    ADDL("}");
    EmitShaderLines(t, "comp", lines, n);
    #undef ADDL
}

// ---- roll state -------------------------------------------------------------

static wchar_t g_rollsDir[MAX_PATH]  = {0};
static wchar_t g_overlayId[32]       = {0};   // "L1-A3F0", empty when no roll is current
static wchar_t g_lastRollPath[MAX_PATH] = {0};
static int     g_haveId              = 0;

static void EnsureRollsDir()
{
    if (g_rollsDir[0]) return;
    // The engine resolves <exe>\Licium\presets\ at init; rolls get their own
    // subfolder there (blessed decision 2), so they stay findable from the normal
    // preset list without burying the user's own presets.
    swprintf(g_rollsDir, L"%sbandit\\", g_plugin.m_szPresetDir);
    CreateDirectoryW(g_rollsDir, NULL);
}

static int WriteFileBytes(const wchar_t* path, const char* data, size_t len)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD written = 0;
    BOOL ok = WriteFile(h, data, (DWORD)len, &written, NULL);
    CloseHandle(h);
    return (ok && written == len) ? 1 : 0;
}

static void CopyToClipboard(HWND hwnd, const wchar_t* text)
{
    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();
    size_t bytes = (wcslen(text) + 1) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (h)
    {
        void* p = GlobalLock(h);
        if (p)
        {
            memcpy(p, text, bytes);
            GlobalUnlock(h);
            SetClipboardData(CF_UNICODETEXT, h);   // clipboard owns h now
        }
        else GlobalFree(h);
    }
    CloseClipboard();
}

extern "C" int BanditRoll(int seed)
{
    unsigned short s;
    if (seed >= 0 && seed <= 0xFFFF)
    {
        s = (unsigned short)seed;
    }
    else
    {
        // Only the *choice* of a fresh seed is allowed to be non-deterministic —
        // everything downstream of it is the fixed LCG.
        LARGE_INTEGER pc;
        QueryPerformanceCounter(&pc);
        s = (unsigned short)((pc.LowPart ^ (pc.LowPart >> 16) ^ GetTickCount()) & 0xFFFF);
    }

    BanditKnobs k;
    RollKnobs(s, &k);

    BanditText t;
    t.Init();
    if (!t.buf) return 0;
    EmitPreset(s, &k, &t);

    EnsureRollsDir();

    wchar_t id[32];
    swprintf(id, L"L%d-%04X", BANDIT_GENVER, s);

    wchar_t path[MAX_PATH];
    swprintf(path, L"%sbandit_%s.milk", g_rollsDir, id);

    int ok = WriteFileBytes(path, t.buf, t.len);
    t.Free();
    if (!ok) return 0;

    // Arm the overlay BEFORE loading: LoadPreset calls back into
    // BanditOnPresetLoaded(), which keeps the ID only for our own roll path.
    lstrcpynW(g_lastRollPath, path, MAX_PATH);
    lstrcpynW(g_overlayId, id, 32);
    g_haveId = 1;

    g_plugin.LoadPreset(path, 0.0f);   // hard cut — a roll should land instantly

    CopyToClipboard(GetActiveWindow(), id);
    return 1;
}

extern "C" const wchar_t* BanditGetOverlayId(void)
{
    return g_haveId ? g_overlayId : NULL;
}

extern "C" void BanditOnPresetLoaded(const wchar_t* szPresetPath)
{
    if (!g_haveId) return;
    // Blessed lifetime: the ID persists until the next preset change. A load of
    // anything other than the roll itself means the roll left the screen.
    if (!szPresetPath || lstrcmpiW(szPresetPath, g_lastRollPath) != 0)
        g_haveId = 0;
}

extern "C" int BanditCopyIdToClipboard(void)
{
    if (!g_haveId) return 0;
    CopyToClipboard(GetActiveWindow(), g_overlayId);
    return 1;
}

extern "C" const wchar_t* BanditGetRollsDir(void)
{
    EnsureRollsDir();
    return g_rollsDir;
}

#endif // MILKDROP_STANDALONE
