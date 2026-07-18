// latent_prospect.cpp — Latent Phase A: the structure prospector.
//
// docs\LATENT_DESIGN.md is the spec; this is a fresh write (blessed decision 2),
// no code carried from anywhere. A standalone CLI with no engine dependency —
// it shares only the fixed-spec headers the other Licium tools use
// (licium_rng.h, licium_palette_lib.h, licium_oklab.h).
//
// What it does: seed -> typed expression tree over the LEAN vocabulary ->
// CPU screens (budget, must-dance, 2D variance, look-space dedup) ->
// ranked queue.jsonl (EVERY passer, forever) -> top-K emitted as .milk rolls
// on the fixed buf0 scaffold. Everything compiles by construction: the grammar
// can only produce legal scalar HLSL.
//
// ---------------------------------------------------------------------------
// LX2 ID CONTRACT (fixed spec — any change here bumps the genver again):
//   * seed: 32 bits, ID printed as LX2-%08X.
//   * LX1 -> LX2 (2026-07-18, Gary's multi-color request): PaletteOfSeed now
//     advances deterministically to the next COLORFUL lib entry (two chromatic
//     colors >= 60 deg apart in oklab hue, chroma >= 0.06), and the comp ramp
//     grew to 4 stops (deep/s1/s2/accent). Grammar, screens, and the LCG
//     stream are UNCHANGED — an LX1 id decodes to the same tree; only the
//     dressing differs. LX1 .milk files on disk stay the archive of v1.
//   * RNG: LiciumRng::Seed32(seed) (licium_rng.h — 0x9E3779B9 ^ seed*2654435761).
//   * Draw order: (1) decay = F(0.960, 0.992); (2) the tree, grown by
//     GrowNode: ONE class draw (Next16 % 100) then ONE kind draw per node,
//     children left -> right, depth-first. The class draw happens even at
//     depths where the class is forced, so the stream never depends on
//     context. CONST draws its value as I(-20,20)/10.
//   * Node kind ids: the NodeKind enum below is frozen.
//   * Lens + palette are SIDE-HASHES of the seed (LensOfSeed / PaletteOfSeed),
//     never the LCG stream — the tree is seed-pure.
//   * The emitted template text below is part of the contract (byte-identical
//     regen is the acceptance test).
// ---------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

#include "../../vis_milk2/licium_rng.h"
#include "../../vis_milk2/licium_oklab.h"
#include "../../vis_milk2/licium_palette_lib.h"

#define LX_GENVER      2
#define LX_EMIT_CAP    200      // emit cap != discard: every passer stays in queue.jsonl
#define LX_MAX_NODES   4096     // pool bound (depth cap keeps real trees far under this)
#define LX_MAX_DEPTH   6        // grammar depth cap (guardrail lives in the TEMPLATE anyway)
#define LX_MIN_SIZE    6        // budget screen: too-small trees are constants in disguise
#define LX_MAX_SIZE    48       // budget screen: unroller safety + legibility

// ---- the lean vocabulary (settled 2026-07-12: force invention) --------------
// Arithmetic atoms + structural essentials only. sin/length/dot/noise stay
// locked until earned via fragment promotion or a deliberate unlock.

enum NodeKind
{
    // binary 0..6
    NK_ADD = 0, NK_SUB, NK_MUL, NK_DIV, NK_MIN, NK_MAX, NK_LT,
    // unary 7..10
    NK_ABS = 7, NK_FRAC, NK_FLOOR, NK_NEG,
    // terminals 11..18
    NK_PX = 11, NK_PY, NK_TIME, NK_BASS, NK_MID, NK_TREB, NK_FB, NK_CONST
};

struct Node
{
    int   kind;
    float cval;     // NK_CONST only
    int   l, r;     // pool indices, -1 = none
};

struct Tree
{
    Node nodes[LX_MAX_NODES];
    int  count;
    int  root;
};

// One class draw + one kind draw per node, always, so the stream is
// context-free (the decode of a chain never depends on depth bookkeeping).
static int GrowNode(LiciumRng* rng, Tree* t, int depth)
{
    if (t->count >= LX_MAX_NODES - 2) return -1;

    int classDraw = (int)(rng->Next16() % 100u);
    int cls;                                  // 0 binary, 1 unary, 2 terminal
    if (depth >= LX_MAX_DEPTH)      cls = 2;  // forced leaf
    else if (depth == 0)            cls = 0;  // root is always an operation
    else if (classDraw < 45)        cls = 0;
    else if (classDraw < 65)        cls = 1;
    else                            cls = 2;

    int i = t->count++;
    Node* n = &t->nodes[i];
    n->l = n->r = -1;
    n->cval = 0.0f;

    if (cls == 0)
    {
        n->kind = NK_ADD + rng->I(0, 6);
        n->l = GrowNode(rng, t, depth + 1);
        n->r = GrowNode(rng, t, depth + 1);
        if (n->l < 0 || n->r < 0) return -1;
    }
    else if (cls == 1)
    {
        n->kind = NK_ABS + rng->I(0, 3);
        n->l = GrowNode(rng, t, depth + 1);
        if (n->l < 0) return -1;
    }
    else
    {
        n->kind = NK_PX + rng->I(0, 7);
        if (n->kind == NK_CONST)
            n->cval = rng->I(-20, 20) / 10.0f;   // quantized: prints exactly, decodes exactly
    }
    return i;
}

// The whole deterministic derivation from a seed: decay, then the tree.
static int BuildFromSeed(unsigned int seed, Tree* t, float* decay)
{
    LiciumRng rng;
    rng.Seed32(seed);
    *decay = rng.F(0.960f, 0.992f);
    t->count = 0;
    t->root = GrowNode(&rng, t, 0);
    return t->root >= 0;
}

// ---- side-hashes (documented contract, independent of the LCG stream) -------

static int LensOfSeed(unsigned int seed)     // 0 sat(f)  1 sat(|f|)  2 sat(1-|f|)
{
    unsigned int h = (seed ^ 0x4C454E53u) * 2246822519u;   // 'LENS'
    unsigned int r = (h >> 8) % 100u;
    return (r < 45u) ? 0 : (r < 75u) ? 1 : 2;
}

// LX2: a palette qualifies only if it carries two genuinely chromatic colors
// on distinct hues (oklab chroma >= 0.06, hue separation >= 60 deg) — tonal
// and near-gray combos read as one color through the ramp and are skipped.
static int PaletteIsColorful(int pi)
{
    const LiciumLibPalette* lp = &LICIUM_PAL_LIB[pi];
    float hue[5];
    int nh = 0;
    for (int i = 0; i < lp->n && i < 5; i++)
    {
        float r = ((lp->c[i] >> 16) & 0xFF) / 255.0f;
        float g = ((lp->c[i] >> 8)  & 0xFF) / 255.0f;
        float b = ( lp->c[i]        & 0xFF) / 255.0f;
        float L, a, bb;
        LicSrgbToOklab(r, g, b, &L, &a, &bb);
        if (sqrtf(a * a + bb * bb) < 0.06f) continue;   // gray-ish: no hue vote
        hue[nh++] = atan2f(bb, a);
    }
    for (int i = 0; i < nh; i++)
        for (int j = i + 1; j < nh; j++)
        {
            float d = fabsf(hue[i] - hue[j]);
            if (d > 3.14159265f) d = 6.28318531f - d;
            if (d >= 1.04719755f) return 1;             // >= 60 degrees apart
        }
    return 0;
}

static int PaletteOfSeed(unsigned int seed)
{
    unsigned int h = (seed ^ 0x50414C45u) * 2246822519u;   // 'PALE'
    const int n = (int)(sizeof(LICIUM_PAL_LIB) / sizeof(LICIUM_PAL_LIB[0]));
    int pi = (int)((h >> 10) % (unsigned int)n);
    // LX2 multi-color guarantee: deterministic advance to the next colorful
    // entry (the lib has many; this terminates long before wrapping).
    for (int k = 0; k < n && !PaletteIsColorful(pi); k++) pi = (pi + 1) % n;
    return pi;
}

// ---- CPU interpreter --------------------------------------------------------
// Mirrors the emitted HLSL exactly (same safe-div epsilon, same lens formulas).
// The screens judge the INK FIELD; the full preset (advection, comp shading)
// is Phase B's job on the real renderer.

struct EvalCtx { float px, py, t, bass, mid, treb, fb; };

static float EvalNode(const Tree* tr, int i, const EvalCtx* c)
{
    const Node* n = &tr->nodes[i];
    switch (n->kind)
    {
    case NK_ADD:  return EvalNode(tr, n->l, c) + EvalNode(tr, n->r, c);
    case NK_SUB:  return EvalNode(tr, n->l, c) - EvalNode(tr, n->r, c);
    case NK_MUL:  return EvalNode(tr, n->l, c) * EvalNode(tr, n->r, c);
    case NK_DIV:  { float b = EvalNode(tr, n->r, c); return EvalNode(tr, n->l, c) / (fabsf(b) + 0.1f); }
    case NK_MIN:  { float a = EvalNode(tr, n->l, c), b = EvalNode(tr, n->r, c); return a < b ? a : b; }
    case NK_MAX:  { float a = EvalNode(tr, n->l, c), b = EvalNode(tr, n->r, c); return a > b ? a : b; }
    case NK_LT:   return EvalNode(tr, n->l, c) < EvalNode(tr, n->r, c) ? 1.0f : 0.0f;
    case NK_ABS:  return fabsf(EvalNode(tr, n->l, c));
    case NK_FRAC: { float a = EvalNode(tr, n->l, c); return a - floorf(a); }
    case NK_FLOOR:return floorf(EvalNode(tr, n->l, c));
    case NK_NEG:  return -EvalNode(tr, n->l, c);
    case NK_PX:   return c->px;
    case NK_PY:   return c->py;
    case NK_TIME: return c->t;
    case NK_BASS: return c->bass;
    case NK_MID:  return c->mid;
    case NK_TREB: return c->treb;
    case NK_FB:   return c->fb;
    default:      return n->cval;
    }
}

static float Sat(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

static float ApplyLens(int lens, float f)
{
    // The LS2 lesson: saturate(f) alone clips ALL negative structure — dark
    // rolls were hiding real behavior. Three lenses, hashed per seed, applied
    // identically here and in the emitted shader.
    if (lens == 0) return Sat(f);
    if (lens == 1) return Sat(fabsf(f));
    return Sat(1.0f - fabsf(f));               // zero-crossing glow
}

static int TreeUses(const Tree* t, int kindLo, int kindHi)
{
    for (int i = 0; i < t->count; i++)
        if (t->nodes[i].kind >= kindLo && t->nodes[i].kind <= kindHi) return 1;
    return 0;
}

// ---- screens ----------------------------------------------------------------
// Two stages: a cheap coarse look (most seeds die here for pennies), then the
// full-grid settle for survivors. Feedback is modeled per-cell WITHOUT
// advection: fb' = fb*decay + inj*lens(f) iterated to (near) equilibrium —
// exactly the equilibrium the template's clamp assumes.

struct ScreenResult
{
    float score;        // ranking key: spatial richness x audio response
    float dance;        // mean |lens(f)| change on an audio transient
    unsigned int hash;  // FNV-1a over the quantized settled field (look-space)
};

// Settle the field at audio level 'aud'; write lens values into out[cells].
static void SettleField(const Tree* t, int lens, float decay, int grid,
                        float aud, float tt, float* out)
{
    const float inj = 1.0f - decay;            // injK = 1 -> equilibrium at ~lens max
    for (int y = 0; y < grid; y++)
    for (int x = 0; x < grid; x++)
    {
        EvalCtx c;
        c.px = -1.0f + 2.0f * (x + 0.5f) / grid;
        c.py = -1.0f + 2.0f * (y + 0.5f) / grid;
        c.t  = tt;
        c.bass = c.mid = c.treb = aud;         // silence pins these to 1.0 in-engine
        float fb = 0.0f;
        float lf = 0.0f;
        for (int k = 0; k < 40; k++)
        {
            c.fb = fb;
            lf = ApplyLens(lens, EvalNode(t, t->root, &c));
            fb = fb * decay + inj * lf;
        }
        out[y * grid + x] = lf;
    }
}

static int ScreenCandidate(const Tree* t, int lens, float decay, ScreenResult* res)
{
    // 1. Budget.
    if (t->count < LX_MIN_SIZE || t->count > LX_MAX_SIZE) return 0;

    // 2. Structural musts: position (else it's a flat wash by construction)
    //    and audio (the must-dance guarantee starts as a static screen).
    if (!TreeUses(t, NK_PX, NK_PY))     return 0;
    if (!TreeUses(t, NK_BASS, NK_TREB)) return 0;

    // 3. Coarse look: 12x12, quick settle — kills flats/blacks/whites cheap.
    {
        float f[12 * 12];
        SettleField(t, lens, decay, 12, 1.0f, 0.5f, f);
        float mean = 0.0f;
        for (int i = 0; i < 144; i++) mean += f[i];
        mean /= 144.0f;
        if (mean < 0.01f || mean > 0.95f) return 0;
        float var = 0.0f;
        for (int i = 0; i < 144; i++) var += (f[i] - mean) * (f[i] - mean);
        if (sqrtf(var / 144.0f) < 0.02f) return 0;
    }

    // 4. Full grid at 32x32.
    static float f0[32 * 32], f1[32 * 32];
    SettleField(t, lens, decay, 32, 1.0f, 0.5f, f0);

    // 2D-axis variance: variation along BOTH axes (kills stripes: a field
    // that only varies in x is a barcode, however lively).
    float rowVar = 0.0f, colVar = 0.0f;
    for (int y = 0; y < 32; y++)
    {
        float m = 0.0f;
        for (int x = 0; x < 32; x++) m += f0[y * 32 + x];
        m /= 32.0f;
        float v = 0.0f;
        for (int x = 0; x < 32; x++) { float d = f0[y * 32 + x] - m; v += d * d; }
        rowVar += v / 32.0f;
    }
    for (int x = 0; x < 32; x++)
    {
        float m = 0.0f;
        for (int y = 0; y < 32; y++) m += f0[y * 32 + x];
        m /= 32.0f;
        float v = 0.0f;
        for (int y = 0; y < 32; y++) { float d = f0[y * 32 + x] - m; v += d * d; }
        colVar += v / 32.0f;
    }
    rowVar = sqrtf(rowVar / 32.0f);
    colVar = sqrtf(colVar / 32.0f);
    if (rowVar < 0.015f || colVar < 0.015f) return 0;

    // Must-dance: an audio transient (bass 1.0 -> 1.35, the in-engine scale of
    // a real beat over the relative baseline) must visibly move the field.
    SettleField(t, lens, decay, 32, 1.35f, 0.5f, f1);
    float dance = 0.0f;
    for (int i = 0; i < 32 * 32; i++) dance += fabsf(f1[i] - f0[i]);
    dance /= 32.0f * 32.0f;
    if (dance < 0.01f) return 0;

    // Look-space hash: FNV-1a over the quantized settled field. Dedup by what
    // it LOOKS like, not how the chain is written.
    unsigned int h = 2166136261u;
    for (int i = 0; i < 32 * 32; i++)
    {
        unsigned char q = (unsigned char)(Sat(f0[i]) * 255.0f + 0.5f);
        q &= 0xF0;                             // coarse: 16 levels
        h = (h ^ q) * 16777619u;
    }

    float spatial = 0.5f * (rowVar + colVar);
    res->score = spatial * (1.0f + 4.0f * (dance > 0.25f ? 0.25f : dance));
    res->dance = dance;
    res->hash  = h;
    return 1;
}

// ---- HLSL emission ----------------------------------------------------------

struct Str
{
    char*  buf;
    size_t len, cap;
    void Init() { cap = 32768; len = 0; buf = (char*)malloc(cap); if (buf) buf[0] = 0; }
    void Free() { free(buf); buf = NULL; }
    void Raw(const char* s)
    {
        size_t n = strlen(s);
        while (len + n + 1 > cap)
        {
            cap *= 2;
            char* nb = (char*)realloc(buf, cap);
            if (!nb) return;
            buf = nb;
        }
        memcpy(buf + len, s, n + 1);
        len += n;
    }
    void Add(const char* fmt, ...)
    {
        char line[2048];
        va_list ap;
        va_start(ap, fmt);
        _vsnprintf(line, sizeof(line) - 3, fmt, ap);
        va_end(ap);
        line[sizeof(line) - 3] = 0;
        strcat(line, "\r\n");
        Raw(line);
    }
};

// Infix with explicit parens; spaces around binary ops so the line-splitter
// below always has a safe break point. Mirrors EvalNode exactly.
static void EmitExpr(const Tree* t, int i, Str* s)
{
    const Node* n = &t->nodes[i];
    char tmp[64];
    switch (n->kind)
    {
    case NK_ADD: s->Raw("("); EmitExpr(t, n->l, s); s->Raw(" + "); EmitExpr(t, n->r, s); s->Raw(")"); break;
    case NK_SUB: s->Raw("("); EmitExpr(t, n->l, s); s->Raw(" - "); EmitExpr(t, n->r, s); s->Raw(")"); break;
    case NK_MUL: s->Raw("("); EmitExpr(t, n->l, s); s->Raw(" * "); EmitExpr(t, n->r, s); s->Raw(")"); break;
    case NK_DIV: s->Raw("("); EmitExpr(t, n->l, s); s->Raw(" / (abs("); EmitExpr(t, n->r, s); s->Raw(") + 0.1))"); break;
    case NK_MIN: s->Raw("min("); EmitExpr(t, n->l, s); s->Raw(", "); EmitExpr(t, n->r, s); s->Raw(")"); break;
    case NK_MAX: s->Raw("max("); EmitExpr(t, n->l, s); s->Raw(", "); EmitExpr(t, n->r, s); s->Raw(")"); break;
    case NK_LT:  s->Raw("(("); EmitExpr(t, n->l, s); s->Raw(" < "); EmitExpr(t, n->r, s); s->Raw(") ? 1.0 : 0.0)"); break;
    case NK_ABS: s->Raw("abs("); EmitExpr(t, n->l, s); s->Raw(")"); break;
    case NK_FRAC:s->Raw("frac("); EmitExpr(t, n->l, s); s->Raw(")"); break;
    case NK_FLOOR:s->Raw("floor("); EmitExpr(t, n->l, s); s->Raw(")"); break;
    case NK_NEG: s->Raw("(-"); EmitExpr(t, n->l, s); s->Raw(")"); break;
    case NK_PX:  s->Raw("p.x"); break;
    case NK_PY:  s->Raw("p.y"); break;
    case NK_TIME:s->Raw("tt"); break;
    case NK_BASS:s->Raw("bass_att"); break;
    case NK_MID: s->Raw("mid_att"); break;
    case NK_TREB:s->Raw("treb_att"); break;
    case NK_FB:  s->Raw("fb"); break;
    default:
        _snprintf(tmp, sizeof(tmp) - 1, "%.1f", n->cval);
        tmp[sizeof(tmp) - 1] = 0;
        s->Raw(tmp);
        break;
    }
}

// The engine concatenates bufN_k lines back into one source, so a long
// expression may span lines — but only split at spaces (never mid-token).
static void EmitShaderLinesSplit(Str* out, const char* prefix, const char* body)
{
    int lineNo = 1;
    const char* s = body;
    while (*s)
    {
        const char* nl = strchr(s, '\n');
        size_t seg = nl ? (size_t)(nl - s) : strlen(s);
        while (seg > 0)
        {
            size_t take = seg;
            if (take > 400)
            {
                take = 400;
                while (take > 0 && s[take] != ' ') take--;   // back up to a space
                if (take == 0) take = 400;                   // no space: hard split (can't happen with our emitter)
            }
            char line[512];
            memcpy(line, s, take);
            line[take] = 0;
            out->Add("%s_%d=`%s", prefix, lineNo++, line);
            s += take;
            seg -= take;
            while (*s == ' ' && seg > 0) { s++; seg--; }     // a split eats the break space
        }
        if (nl) s = nl + 1; else break;
    }
}

static void EmitPreset(unsigned int seed, const Tree* t, float decay, Str* out)
{
    const int lens = LensOfSeed(seed);
    const int pi   = PaletteOfSeed(seed);

    // Palette: curated lib pick -> luminance-sort -> the LS2 dressing law
    // (deep = darkest*0.30 tinted floor, mid = middle, accent = brightest).
    const LiciumLibPalette* lp = &LICIUM_PAL_LIB[pi];
    float rgb[5][3];
    int pn = lp->n;
    for (int i = 0; i < pn; i++)
    {
        rgb[i][0] = ((lp->c[i] >> 16) & 0xFF) / 255.0f;
        rgb[i][1] = ((lp->c[i] >> 8)  & 0xFF) / 255.0f;
        rgb[i][2] = ( lp->c[i]        & 0xFF) / 255.0f;
    }
    for (int i = 1; i < pn; i++)
        for (int j = i; j > 0 && LicSrgbLum(rgb[j]) < LicSrgbLum(rgb[j - 1]); j--)
            for (int c = 0; c < 3; c++)
            { float x = rgb[j][c]; rgb[j][c] = rgb[j - 1][c]; rgb[j - 1][c] = x; }
    // LX2: FOUR ramp stops (deep + two interior thirds + accent) so the ramp
    // traverses more of the palette's hues, not just its luminance ladder.
    // For pn=3 both interior stops collapse to the middle color (graceful).
    const int i1 = ((pn - 1) + 1) / 3;
    const int i2 = (2 * (pn - 1) + 1) / 3;
    float deep[3], s1[3], s2[3], accent[3], ink[3];
    for (int c = 0; c < 3; c++)
    {
        deep[c]   = rgb[0][c] * 0.30f;
        s1[c]     = rgb[i1][c];
        s2[c]     = rgb[i2][c];
        accent[c] = rgb[pn - 1][c];
        ink[c]    = accent[c] * 0.75f + s2[c] * 0.25f;    // fresh injection reads bright
    }
    float palMax = accent[0];
    if (accent[1] > palMax) palMax = accent[1];
    if (accent[2] > palMax) palMax = accent[2];
    const float clampMax = palMax * 1.15f;
    const float inj = 1.0f - decay;                        // injK = 1: settles at ~lens max

    out->Add("MILKDROP_PRESET_VERSION=201");
    out->Add("PSVERSION=3");
    out->Add("PSVERSION_WARP=3");
    out->Add("PSVERSION_COMP=3");
    out->Add("[preset00]");
    out->Add("fRating=3.000");
    out->Add("fGammaAdj=1.000");
    out->Add("fDecay=0.980");
    out->Add("nWaveMode=0");
    out->Add("fWaveAlpha=0.000");
    out->Add("bDarkenCenter=0");
    out->Add("zoom=1.00000");
    out->Add("rot=0.00000");
    out->Add("warp=0.00000");
    out->Add("buf0_iterations=1");

    // --- buf0: fixed medium, evolved ink -------------------------------------
    // The template is the guardrail: gentle fixed swirl (divergence-free curl),
    // decay, equilibrium-scaled injection, hue-preserving clamp. The evolved
    // tree only shapes WHERE ink appears. fb is read at uv (pre-advection),
    // matching the CPU interpreter's feedback model.
    {
        Str body;
        body.Init();
        Str expr;
        expr.Init();
        EmitExpr(t, t->root, &expr);

        body.Raw("shader_body\n{\n");
        body.Raw("float2 p = (uv - 0.5) * float2(texsize.x*texsize.w, 1.0) * 2.0;\n");
        body.Raw("float tt = time * 0.25;\n");
        body.Raw("float3 prevc = tex2D(sampler_buf0, uv).xyz;\n");
        body.Raw("float fb = dot(prevc, float3(0.32, 0.49, 0.29));\n");
        body.Raw("float f = ");
        body.Raw(expr.buf ? expr.buf : "0.0");
        body.Raw(";\n");
        if (lens == 0)      body.Raw("f = saturate(f);\n");
        else if (lens == 1) body.Raw("f = saturate(abs(f));\n");
        else                body.Raw("f = saturate(1.0 - abs(f));\n");
        body.Raw("float2 vel = float2(-2.3*sin(p.y*2.3 + tt*1.7), 2.1*sin(p.x*2.1 - tt*1.3));\n");
        body.Raw("float2 suv = uv - vel * 0.0035;\n");
        body.Raw("float3 prev = tex2D(sampler_buf0, suv).xyz;\n");
        {
            char l[512];
            _snprintf(l, sizeof(l) - 1,
                "float3 acc = prev*%.5f + float3(%.4f, %.4f, %.4f) * f * %.6f;\n",
                decay, ink[0], ink[1], ink[2], inj);
            l[sizeof(l) - 1] = 0;
            body.Raw(l);
            body.Raw("float m = max(acc.x, max(acc.y, acc.z));\n");
            _snprintf(l, sizeof(l) - 1, "acc *= min(1.0, %.5f / (m + 0.0001));\n", clampMax);
            l[sizeof(l) - 1] = 0;
            body.Raw(l);
        }
        body.Raw("ret = acc;\n}");
        EmitShaderLinesSplit(out, "buf0", body.buf ? body.buf : "");
        expr.Free();
        body.Free();
    }

    // --- warp: carrier only --------------------------------------------------
    out->Add("warp_1=`shader_body");
    out->Add("warp_2=`{");
    out->Add("warp_3=`    ret = tex2D(sampler_main, uv).xyz * 0.0;");
    out->Add("warp_4=`}");

    // --- comp: gradient-sheen shading through the ramp (fixed knobs) ---------
    {
        Str body;
        body.Init();
        char l[512];
        body.Raw("shader_body\n{\n");
        body.Raw("float2 d = texsize_buf0.zw;\n");
        body.Raw("float3 c = tex2D(sampler_buf0, uv).xyz;\n");
        body.Raw("float3 lw = float3(0.32, 0.49, 0.29);\n");
        body.Raw("float lx = dot(tex2D(sampler_buf0, uv + float2(d.x,0)).xyz, lw) - dot(tex2D(sampler_buf0, uv - float2(d.x,0)).xyz, lw);\n");
        body.Raw("float ly = dot(tex2D(sampler_buf0, uv + float2(0,d.y)).xyz, lw) - dot(tex2D(sampler_buf0, uv - float2(0,d.y)).xyz, lw);\n");
        body.Raw("float3 nrm = normalize(float3(-lx*3.5, -ly*3.5, 1.0));\n");
        body.Raw("float3 lgt = normalize(float3(cos(time*0.23), sin(time*0.19), 0.55 + 0.45*mid_att));\n");
        body.Raw("float sheen = pow(saturate(dot(nrm, lgt)), 2.5);\n");
        body.Raw("float dens = saturate(dot(c, lw) * 3.0);\n");
        _snprintf(l, sizeof(l) - 1,
            "float3 ramp = lerp(float3(%.4f,%.4f,%.4f), float3(%.4f,%.4f,%.4f), saturate(dens*3.0));\n",
            deep[0], deep[1], deep[2], s1[0], s1[1], s1[2]);
        l[sizeof(l) - 1] = 0; body.Raw(l);
        _snprintf(l, sizeof(l) - 1,
            "ramp = lerp(ramp, float3(%.4f,%.4f,%.4f), saturate(dens*3.0 - 1.0));\n",
            s2[0], s2[1], s2[2]);
        l[sizeof(l) - 1] = 0; body.Raw(l);
        _snprintf(l, sizeof(l) - 1,
            "ramp = lerp(ramp, float3(%.4f,%.4f,%.4f), saturate(dens*3.0 - 2.0));\n",
            accent[0], accent[1], accent[2]);
        l[sizeof(l) - 1] = 0; body.Raw(l);
        _snprintf(l, sizeof(l) - 1,
            "ret = ramp + float3(%.4f,%.4f,%.4f) * sheen * dens * 0.5;\n",
            accent[0], accent[1], accent[2]);
        l[sizeof(l) - 1] = 0; body.Raw(l);
        body.Raw("}");
        EmitShaderLinesSplit(out, "comp", body.buf ? body.buf : "");
        body.Free();
    }
}

// ---- decode -----------------------------------------------------------------

static void PrintChain(const Tree* t, int i)
{
    const Node* n = &t->nodes[i];
    if (n->kind == NK_CONST) printf("%d(%.1f) ", n->kind, n->cval);
    else                     printf("%d ", n->kind);
    if (n->l >= 0) PrintChain(t, n->l);
    if (n->r >= 0) PrintChain(t, n->r);
}

static void PrintInfix(const Tree* t)
{
    Str s;
    s.Init();
    EmitExpr(t, t->root, &s);
    printf("  f = %s\n", s.buf ? s.buf : "?");
    s.Free();
}

// ---- scan / files -----------------------------------------------------------

struct Passer
{
    unsigned int seed;
    float score;
    float dance;
    int   nodes;
    unsigned int hash;
};

static int CmpPasser(const void* a, const void* b)
{
    const Passer* pa = (const Passer*)a;
    const Passer* pb = (const Passer*)b;
    if (pa->score > pb->score) return -1;
    if (pa->score < pb->score) return 1;
    return pa->seed < pb->seed ? -1 : (pa->seed > pb->seed ? 1 : 0);   // stable, deterministic
}

static int WriteBytes(const char* path, const char* data, size_t len)
{
    FILE* fp = fopen(path, "wb");
    if (!fp) return 0;
    size_t w = fwrite(data, 1, len, fp);
    fclose(fp);
    return w == len;
}

static int EmitRollFile(unsigned int seed, const char* outdir, char* pathOut, size_t pathCap)
{
    Tree t;
    float decay;
    if (!BuildFromSeed(seed, &t, &decay)) return 0;
    Str s;
    s.Init();
    if (!s.buf) return 0;
    EmitPreset(seed, &t, decay, &s);
    _snprintf(pathOut, pathCap - 1, "%s\\latent_LX%d-%08X.milk", outdir, LX_GENVER, seed);
    pathOut[pathCap - 1] = 0;
    int ok = WriteBytes(pathOut, s.buf, s.len);
    s.Free();
    return ok;
}

static int SelfTest(void)
{
    // Doc acceptance test: same seed twice -> byte-identical emission.
    const unsigned int seeds[] = { 0x00000001u, 0xDEADBEEFu, 0x12345678u, 0xA5A5A5A5u };
    for (int i = 0; i < 4; i++)
    {
        Tree t1, t2;
        float d1, d2;
        if (!BuildFromSeed(seeds[i], &t1, &d1) || !BuildFromSeed(seeds[i], &t2, &d2)) return 0;
        Str a, b;
        a.Init(); b.Init();
        EmitPreset(seeds[i], &t1, d1, &a);
        EmitPreset(seeds[i], &t2, d2, &b);
        int same = (a.len == b.len) && memcmp(a.buf, b.buf, a.len) == 0;
        a.Free(); b.Free();
        if (!same) { printf("SELFTEST FAIL seed %08X\n", seeds[i]); return 0; }
    }
    printf("selftest: 4 seeds, double emission byte-identical. PASS\n");
    return 1;
}

// A tiny open-addressing set for look-space dedup within a run.
#define DEDUP_BITS 20
#define DEDUP_SIZE (1u << DEDUP_BITS)
static unsigned int g_dedup[DEDUP_SIZE];   // 0 = empty (hash 0 remapped to 1)

static int DedupSeen(unsigned int h)
{
    if (h == 0) h = 1;
    unsigned int i = h & (DEDUP_SIZE - 1);
    for (unsigned int probe = 0; probe < 64; probe++)
    {
        unsigned int v = g_dedup[i];
        if (v == h) return 1;
        if (v == 0) { g_dedup[i] = h; return 0; }
        i = (i + 1) & (DEDUP_SIZE - 1);
    }
    return 0;   // crowded neighborhood: admit rather than drop (dedup is advisory)
}

int main(int argc, char** argv)
{
    unsigned int start = 0, count = 0, single = 0;
    int haveSingle = 0, haveDecode = 0, emitCap = LX_EMIT_CAP;
    const char* outdir = NULL;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--selftest")) return SelfTest() ? 0 : 1;
        else if (!strcmp(argv[i], "--decode") && i + 1 < argc) { single = (unsigned int)strtoul(argv[++i], NULL, 16); haveDecode = 1; }
        else if (!strcmp(argv[i], "--seed")   && i + 1 < argc) { single = (unsigned int)strtoul(argv[++i], NULL, 16); haveSingle = 1; }
        else if (!strcmp(argv[i], "--start")  && i + 1 < argc) start  = (unsigned int)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--count")  && i + 1 < argc) count  = (unsigned int)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--emit")   && i + 1 < argc) emitCap = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--outdir") && i + 1 < argc) outdir = argv[++i];
        else { printf("unknown arg: %s\n", argv[i]); return 1; }
    }

    if (haveDecode)
    {
        Tree t;
        float decay;
        if (!BuildFromSeed(single, &t, &decay)) { printf("grow failed (pool bound)\n"); return 1; }
        printf("LX%d-%08X\n", LX_GENVER, single);
        printf("  decay = %.5f  lens = %d  palette = %d  nodes = %d\n",
               decay, LensOfSeed(single), PaletteOfSeed(single), t.count);
        printf("  chain: ");
        PrintChain(&t, t.root);
        printf("\n");
        PrintInfix(&t);
        return 0;
    }

    if (haveSingle)
    {
        if (!outdir) { printf("--seed needs --outdir\n"); return 1; }
        Tree t;
        float decay;
        if (!BuildFromSeed(single, &t, &decay)) { printf("grow failed\n"); return 1; }
        ScreenResult r;
        int pass = ScreenCandidate(&t, LensOfSeed(single), decay, &r);
        char path[1024];
        if (!EmitRollFile(single, outdir, path, sizeof(path))) { printf("emit failed\n"); return 1; }
        printf("LX%d-%08X  nodes=%d  screen=%s", LX_GENVER, single, t.count, pass ? "PASS" : "reject");
        if (pass) printf("  score=%.4f dance=%.4f", r.score, r.dance);
        printf("\n  emitted (forced): %s\n", path);
        return 0;
    }

    if (!count || !outdir)
    {
        printf("latent_prospect (LX%d) — Latent Phase A. docs\\LATENT_DESIGN.md\n", LX_GENVER);
        printf("  --scan:     --start N --count N --outdir DIR [--emit K (default %d)]\n", LX_EMIT_CAP);
        printf("  --seed HEX --outdir DIR   screen + force-emit one seed\n");
        printf("  --decode HEX              print chain + formula\n");
        printf("  --selftest                byte-identical double-emission check\n");
        return 1;
    }

    memset(g_dedup, 0, sizeof(g_dedup));
    Passer* passers = (Passer*)malloc(sizeof(Passer) * 65536);
    int np = 0, capP = 65536;
    unsigned int nGrown = 0, nScreened = 0, nDeduped = 0;

    for (unsigned int k = 0; k < count; k++)
    {
        unsigned int seed = start + k;
        Tree t;
        float decay;
        if (!BuildFromSeed(seed, &t, &decay)) continue;
        nGrown++;
        ScreenResult r;
        if (!ScreenCandidate(&t, LensOfSeed(seed), decay, &r)) continue;
        nScreened++;
        if (DedupSeen(r.hash)) { nDeduped++; continue; }
        if (np == capP)
        {
            capP *= 2;
            Passer* nb = (Passer*)realloc(passers, sizeof(Passer) * capP);
            if (!nb) break;
            passers = nb;
        }
        passers[np].seed  = seed;
        passers[np].score = r.score;
        passers[np].dance = r.dance;
        passers[np].nodes = t.count;
        passers[np].hash  = r.hash;
        np++;
        if ((k & 0xFFFF) == 0xFFFF)
            printf("  ...%u/%u scanned, %d passers\n", k + 1, count, np);
    }

    qsort(passers, np, sizeof(Passer), CmpPasser);

    // queue.jsonl: EVERY passer, ranked, appended (nothing is ever deleted —
    // this file is the archive; the emit cap only limits .milk files written).
    char qpath[1024];
    _snprintf(qpath, sizeof(qpath) - 1, "%s\\queue.jsonl", outdir);
    qpath[sizeof(qpath) - 1] = 0;
    FILE* q = fopen(qpath, "ab");
    int nEmitted = 0;
    for (int i = 0; i < np; i++)
    {
        int emit = (nEmitted < emitCap);
        char path[1024];
        if (emit && EmitRollFile(passers[i].seed, outdir, path, sizeof(path)))
            nEmitted++;
        else
            emit = 0;
        if (q)
            fprintf(q, "{\"id\":\"LX%d-%08X\",\"score\":%.5f,\"dance\":%.5f,\"nodes\":%d,\"hash\":\"%08X\",\"emitted\":%d}\n",
                    LX_GENVER, passers[i].seed, passers[i].score, passers[i].dance,
                    passers[i].nodes, passers[i].hash, emit);
    }
    if (q) fclose(q);

    printf("scan [%u..%u): %u grown, %u passed screens, %u dedup-dropped, %d queued, %d emitted -> %s\n",
           start, start + count, nGrown, nScreened, nDeduped, np, nEmitted, outdir);
    free(passers);
    return 0;
}
