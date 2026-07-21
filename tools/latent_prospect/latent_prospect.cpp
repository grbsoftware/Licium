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
#include <thread>
#include <atomic>
#include <vector>

#include "../../vis_milk2/licium_rng.h"
#include "../../vis_milk2/licium_oklab.h"
#include "../../vis_milk2/licium_palette_lib.h"

#define LX_GENVER      3
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

// ---- swarm mode (Gary, 2026-07-18 evening: revive the heritage doc's ACO
// core — "build swarm into this and see what it does"; cold stays the default
// cadence). Pheromone table (parent kind, child slot, child kind) -> weight,
// derived STATELESS at scan start from latent_votes.txt + latent_verdicts.jsonl
// (like +3, latebloom +1.5, alive +1, frozen/black -1, blowout -1.5; pass
// votes deposit NOTHING — the stub-pile law). The bias only deflects the kind
// draw where pheromone exists: a flat context reduces to the EXACT cold
// arithmetic (same single Next16 consumed), so a swarm scan over an empty
// state equals the cold scan tree-for-tree. Lineage LXS1: regeneration =
// seed + the run's archived table manifest; the .milk file is the artifact.

#define SWARM_ROOT 19                    // parent id for the root draw
static float g_pher[20][2][19];          // [parent kind|ROOT][child slot][child kind]
static int   g_swarmOn = 0;
static float g_lambda  = 0.35f;
static int   g_seekThreads = 0;          // --seek threads: rank by filament-ness

static int BiasedPick(LiciumRng* rng, int base, int n, int parent, int slot)
{
    const float* w = g_pher[parent][slot];
    float mn = 1e9f, mx = -1e9f;
    if (g_swarmOn)
        for (int k = 0; k < n; k++)
        {
            float v = w[base + k];
            if (v < mn) mn = v;
            if (v > mx) mx = v;
        }
    if (!g_swarmOn || mx - mn < 1e-6f)   // flat context = cold arithmetic
        return base + rng->I(0, n - 1);

    unsigned int wi[19], total = 0;
    for (int k = 0; k < n; k++)
    {
        float v = w[base + k];
        if (v >  2.5f) v =  2.5f;
        if (v < -2.5f) v = -2.5f;
        float f = 1.0f + g_lambda * v;
        if (f < 0.05f) f = 0.05f;        // exploration floor: nothing hits zero
        wi[k] = (unsigned int)(f * 1000.0f + 0.5f);
        total += wi[k];
    }
    unsigned int r = rng->Next16() % total;   // still exactly one draw per node
    for (int k = 0; k < n; k++)
    {
        if (r < wi[k]) return base + k;
        r -= wi[k];
    }
    return base + n - 1;
}

// One class draw + one kind draw per node, always, so the stream is
// context-free (the decode of a chain never depends on depth bookkeeping).
static int GrowNode(LiciumRng* rng, Tree* t, int depth, int parent, int slot)
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
        n->kind = BiasedPick(rng, NK_ADD, 7, parent, slot);
        n->l = GrowNode(rng, t, depth + 1, n->kind, 0);
        n->r = GrowNode(rng, t, depth + 1, n->kind, 1);
        if (n->l < 0 || n->r < 0) return -1;
    }
    else if (cls == 1)
    {
        n->kind = BiasedPick(rng, NK_ABS, 4, parent, slot);
        n->l = GrowNode(rng, t, depth + 1, n->kind, 0);
        if (n->l < 0) return -1;
    }
    else
    {
        n->kind = BiasedPick(rng, NK_PX, 8, parent, slot);
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
    t->root = GrowNode(&rng, t, 0, SWARM_ROOT, 0);
    return t->root >= 0;
}

// Deposit a rated tree's links into the pheromone table.
static void DepositNode(const Tree* t, int i, int parent, int slot, float w)
{
    if (i < 0) return;
    const Node* n = &t->nodes[i];
    g_pher[parent][slot][n->kind] += w;
    DepositNode(t, n->l, n->kind, 0, w);
    DepositNode(t, n->r, n->kind, 1, w);
}

// Ground-truth reader for swarm mode: latent_votes.txt ("ID 1|0|-1") and
// latent_verdicts.jsonl from the Licium state dir, later-wins per id, then
// each rated LX1/LX2 id is COLD-decoded (call this before g_swarmOn goes on)
// and its links deposited. LXS ids are skipped in v1 (their trees need their
// own run manifests to regenerate — counted and reported, never guessed).
struct SwarmRated { char id[24]; float voteW, verdW; };
static int SwarmFindOrAdd(SwarmRated* arr, int* n, int cap, const char* id)
{
    for (int i = 0; i < *n; i++)
        if (!strcmp(arr[i].id, id)) return i;
    if (*n >= cap) return -1;
    strncpy(arr[*n].id, id, 23);
    arr[*n].id[23] = 0;
    arr[*n].voteW = arr[*n].verdW = 0.0f;
    return (*n)++;
}

static void LoadSwarmState(const char* stateDir, int* outDeposited, int* outForeign)
{
    static SwarmRated rated[4096];
    int nr = 0;
    char path[1024], line[512];

    _snprintf(path, sizeof(path) - 1, "%s\\latent_votes.txt", stateDir);
    path[sizeof(path) - 1] = 0;
    FILE* f = fopen(path, "rb");
    if (f)
    {
        char id[24]; int v;
        while (fgets(line, sizeof(line), f))
            if (sscanf(line, "%23s %d", id, &v) == 2)
            {
                int i = SwarmFindOrAdd(rated, &nr, 4096, id);
                if (i >= 0) rated[i].voteW = (v == 1) ? 3.0f : 0.0f;   // stub-pile law: pass = 0
            }
        fclose(f);
    }

    _snprintf(path, sizeof(path) - 1, "%s\\latent_verdicts.jsonl", stateDir);
    path[sizeof(path) - 1] = 0;
    f = fopen(path, "rb");
    if (f)
    {
        while (fgets(line, sizeof(line), f))
        {
            char* idp = strstr(line, "\"id\":\"");
            char* vp  = strstr(line, "\"verdict\":\"");
            if (!idp || !vp) continue;
            idp += 6;
            char id[24]; int k = 0;
            while (idp[k] && idp[k] != '"' && k < 23) { id[k] = idp[k]; k++; }
            id[k] = 0;
            vp += 11;
            float w = 0.0f;
            if      (!strncmp(vp, "latebloom", 9)) w =  1.5f;
            else if (!strncmp(vp, "alive",     5)) w =  1.0f;
            else if (!strncmp(vp, "frozen",    6)) w = -1.0f;
            else if (!strncmp(vp, "black",     5)) w = -1.0f;
            else if (!strncmp(vp, "blowout",   7)) w = -1.5f;
            int i = SwarmFindOrAdd(rated, &nr, 4096, id);
            if (i >= 0) rated[i].verdW = w;                            // later lines win
        }
        fclose(f);
    }

    *outDeposited = 0;
    *outForeign = 0;
    for (int i = 0; i < nr; i++)
    {
        float w = rated[i].voteW + rated[i].verdW;
        if (w == 0.0f) continue;
        // only pure-seed lineages decode cold; LXS/foreign ids are skipped
        unsigned int seed;
        if (!strncmp(rated[i].id, "LX1-", 4) || !strncmp(rated[i].id, "LX2-", 4))
            seed = (unsigned int)strtoul(rated[i].id + 4, NULL, 16);
        else { (*outForeign)++; continue; }
        Tree t;
        float decay;
        if (!BuildFromSeed(seed, &t, &decay)) continue;
        DepositNode(&t, t.root, SWARM_ROOT, 0, w);
        (*outDeposited)++;
    }
}

// Lineage tag for filenames / queue ids: cold = LX<genver>, swarm = LXS1.
static const char* LineageStr(void)
{
    static char cold[8] = { 0 };
    if (g_swarmOn) return "LXS1";
    if (!cold[0]) _snprintf(cold, sizeof(cold) - 1, "LX%d", LX_GENVER);
    return cold;
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
    float f0[32 * 32], f1[32 * 32];   // per-call: ScreenCandidate runs on worker threads
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

    // --seek threads (Gary, 2026-07-18: "a version that searches for long
    // threads" — fitting hunt for an app named after the loom thread).
    // RANK-side only: seed -> shader unchanged, no lineage change; this just
    // decides which passers surface. Ridge cells = local max across exactly
    // ONE axis (a filament crest); peaks = local max on both (blob centers).
    // Fields rich in crests and poor in peaks are thread-worlds. Thresholds
    // are v1 guesses, tuned against real hauls (doc-amendment convention).
    if (g_seekThreads)
    {
        int nRidge = 0, nPeak = 0;
        for (int y = 1; y < 31; y++)
        for (int x = 1; x < 31; x++)
        {
            float c = f0[y * 32 + x];
            if (c < 0.15f) continue;
            int mx = (c > f0[y * 32 + x - 1] && c > f0[y * 32 + x + 1]);
            int my = (c > f0[(y - 1) * 32 + x] && c > f0[(y + 1) * 32 + x]);
            if (mx ^ my) nRidge++;
            else if (mx && my) nPeak++;
        }
        float T = nRidge / 40.0f;
        if (T > 1.0f) T = 1.0f;
        float blobby = nPeak / 12.0f;
        if (blobby > 1.0f) blobby = 1.0f;
        T *= 1.0f - 0.5f * blobby;
        res->score *= 0.2f + 1.8f * T;     // thread-rich x2, thread-poor x0.2
    }

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

// ---- rung 1 (LX3, 2026-07-19): the medium is rolled, not fixed --------------
// Gary's n=273 readout on LX2 ("every single one spirals in the same
// direction"): the grammar only ever decorated ONE hard-coded swirl field.
// A side-hash of the seed (never the tree's LCG stream — same seed still
// grows the same formula as LX2) now picks the velocity archetype, field
// scale, tempo, and spin direction: 4*3*3*2 = 72 mediums. The archetypes are
// Bandit's four divergence-free curls (vel = (dphi/dy, -dphi/dx)) — the
// af6df86 variety lesson, applied to Latent's scaffold.
static void MediumOfSeed(unsigned int seed, int* arche, float* scale,
                         float* speed, float* dir)
{
    unsigned int mh = (seed ^ 0x4D454431u) * 2654435761u;   // 'MED1' salt
    *arche = (int)((mh >> 3) & 3);
    *scale = 1.30f + 1.25f * (float)((mh >> 5) % 3);        // 1.30 2.55 3.80
    *speed = 0.12f + 0.11f * (float)((mh >> 7) % 3);        // 0.12 0.23 0.34
    *dir   = ((mh >> 9) & 1) ? 1.0f : -1.0f;
}
static const char* MediumName(int arche)
{
    static const char* names[4] = { "swirl", "lattice", "stream", "driftweave" };
    return names[arche & 3];
}

// Palette override (browser palette-cycling, 2026-07-21): when set, EmitPreset
// dresses with these exact colors instead of the seed's lib pick — the roll's
// tree/lens/medium are unchanged, only the palette differs. Set from
// --palette-colors; g_palOverrideN == 0 means "use the seed's palette".
static int          g_palOverrideN = 0;
static unsigned int g_palOverride[5];

static void EmitPreset(unsigned int seed, const Tree* t, float decay, Str* out)
{
    const int lens = LensOfSeed(seed);
    const int pi   = PaletteOfSeed(seed);

    // Palette: curated lib pick (or an explicit override) -> luminance-sort ->
    // the LS2 dressing law (deep = darkest*0.30 tinted floor, mid, accent).
    const LiciumLibPalette* lp = &LICIUM_PAL_LIB[pi];
    float rgb[5][3];
    int pn = g_palOverrideN ? g_palOverrideN : lp->n;
    for (int i = 0; i < pn; i++)
    {
        unsigned int c = g_palOverrideN ? g_palOverride[i] : lp->c[i];
        rgb[i][0] = ((c >> 16) & 0xFF) / 255.0f;
        rgb[i][1] = ((c >> 8)  & 0xFF) / 255.0f;
        rgb[i][2] = ( c        & 0xFF) / 255.0f;
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
        {
            int arche; float fscale, fspeed, fdir;
            MediumOfSeed(seed, &arche, &fscale, &fspeed, &fdir);
            char l[256];
            _snprintf(l, sizeof(l) - 1, "float tm = time * %.5f;\nfloat fa = %.5f;\n",
                      fspeed, fscale);
            l[sizeof(l) - 1] = 0;
            body.Raw(l);
            switch (arche)
            {
            default:
            case 0: // SWIRL — one dominant rotation cell (the LX2 medium's family)
                body.Raw("float s1 = sin(p.x*fa + tm), c1 = cos(p.x*fa + tm);\n"
                         "float s2 = sin(p.y*fa*0.8 - tm*0.7), c2 = cos(p.y*fa*0.8 - tm*0.7);\n"
                         "float c3 = cos((p.x + p.y)*fa*0.6 + tm*1.3);\n"
                         "float dphidx = fa*c1*c2 + 0.3*fa*c3;\n"
                         "float dphidy = -fa*0.8*s1*s2 + 0.3*fa*c3;\n");
                break;
            case 1: // LATTICE — counter-rotating cells, dye weaves between them
                body.Raw("float dphidx = fa*cos(p.x*fa + tm)*sin(p.y*fa - tm*0.8);\n"
                         "float dphidy = fa*sin(p.x*fa + tm)*cos(p.y*fa - tm*0.8);\n");
                break;
            case 2: // STREAM — meandering horizontal jet, vertical wander
                body.Raw("float dphidx = -0.2*fa*sin(p.x*fa*0.5 + tm*0.8);\n"
                         "float dphidy = -1.8*fa*sin(p.y*fa*1.5 - tm);\n");
                break;
            case 3: // DRIFT-WEAVE — the lattice plus steady linear drift
                body.Raw("float dphidx = 0.6*fa*cos(p.x*fa + tm)*sin(p.y*fa - tm*0.8) + 1.1;\n"
                         "float dphidy = 0.6*fa*sin(p.x*fa + tm)*cos(p.y*fa - tm*0.8);\n");
                break;
            }
            // /fa: tempo must not ride the cell size; *fdir: mirrored spin twin
            _snprintf(l, sizeof(l) - 1,
                      "float2 vel = float2(dphidy, -dphidx) * %.5f;\n", 2.2f * fdir / fscale);
            l[sizeof(l) - 1] = 0;
            body.Raw(l);
        }
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

static int EmitRollFile(unsigned int seed, const char* outdir, char* pathOut, size_t pathCap,
                        const char* forcePath = NULL)
{
    Tree t;
    float decay;
    if (!BuildFromSeed(seed, &t, &decay)) return 0;
    Str s;
    s.Init();
    if (!s.buf) return 0;
    EmitPreset(seed, &t, decay, &s);
    if (forcePath)
    {
        _snprintf(pathOut, pathCap - 1, "%s", forcePath);   // exact path (palette re-dress)
    }
    else
    {
        _snprintf(pathOut, pathCap - 1, "%s\\latent_%s-%08X.milk", outdir, LineageStr(), seed);
    }
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
    int nThreads = 0;                  // 0 = auto (cores - 2, min 1)
    const char* outdir = NULL;
    const char* swarmDir = NULL;       // --swarm <Licium state dir> = LXS1 mode
    const char* outfile = NULL;        // --outfile PATH: exact path for --seed

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--selftest")) return SelfTest() ? 0 : 1;
        else if (!strcmp(argv[i], "--outfile") && i + 1 < argc) outfile = argv[++i];
        else if (!strcmp(argv[i], "--palette-colors") && i + 1 < argc)
        {
            // "RRGGBB,RRGGBB,..." (1-5) — re-dress a roll with an exact palette.
            g_palOverrideN = 0;
            const char* s = argv[++i];
            while (*s && g_palOverrideN < 5)
            {
                g_palOverride[g_palOverrideN++] = (unsigned int)strtoul(s, NULL, 16);
                const char* comma = strchr(s, ',');
                if (!comma) break;
                s = comma + 1;
            }
        }
        else if (!strcmp(argv[i], "--decode") && i + 1 < argc) { single = (unsigned int)strtoul(argv[++i], NULL, 16); haveDecode = 1; }
        else if (!strcmp(argv[i], "--seed")   && i + 1 < argc) { single = (unsigned int)strtoul(argv[++i], NULL, 16); haveSingle = 1; }
        else if (!strcmp(argv[i], "--start")  && i + 1 < argc) start  = (unsigned int)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--count")  && i + 1 < argc) count  = (unsigned int)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--emit")   && i + 1 < argc) emitCap = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--threads") && i + 1 < argc) nThreads = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--swarm")  && i + 1 < argc) swarmDir = argv[++i];
        else if (!strcmp(argv[i], "--lambda") && i + 1 < argc) g_lambda = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--seek")   && i + 1 < argc)
        {
            const char* profile = argv[++i];
            if (!strcmp(profile, "threads")) g_seekThreads = 1;
            else { printf("unknown seek profile: %s (profiles: threads)\n", profile); return 1; }
        }
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
        if (!outdir && !outfile) { printf("--seed needs --outdir or --outfile\n"); return 1; }
        Tree t;
        float decay;
        if (!BuildFromSeed(single, &t, &decay)) { printf("grow failed\n"); return 1; }
        ScreenResult r;
        int pass = ScreenCandidate(&t, LensOfSeed(single), decay, &r);
        char path[1024];
        if (!EmitRollFile(single, outdir, path, sizeof(path), outfile)) { printf("emit failed\n"); return 1; }
        {
            int arche; float fscale, fspeed, fdir;
            MediumOfSeed(single, &arche, &fscale, &fspeed, &fdir);
            printf("LX%d-%08X  medium=%s a=%.2f spd=%.2f dir=%c  nodes=%d  screen=%s",
                   LX_GENVER, single, MediumName(arche), fscale, fspeed,
                   (fdir > 0) ? '+' : '-', t.count, pass ? "PASS" : "reject");
        }
        if (pass) printf("  score=%.4f dance=%.4f", r.score, r.dance);
        printf("\n  emitted (forced): %s\n", path);
        return 0;
    }

    if (!count || !outdir)
    {
        printf("latent_prospect (LX%d) — Latent Phase A. docs\\LATENT_DESIGN.md\n", LX_GENVER);
        printf("  --scan:     --start N --count N --outdir DIR [--emit K (default %d)] [--threads N (default cores-2)]\n", LX_EMIT_CAP);
        printf("  --swarm STATEDIR [--lambda F (default 0.35)]   ACO mode: pheromone from\n");
        printf("              votes+verdicts in STATEDIR biases growth; lineage LXS1;\n");
        printf("              table manifest archived next to the queue\n");
        printf("  --seek threads            rank-side profile: surface filament/thread\n");
        printf("              visuals (ridge-rich, blob-poor fields); combinable with --swarm\n");
        printf("  --seed HEX --outdir DIR   screen + force-emit one seed\n");
        printf("  --decode HEX              print chain + formula (cold lineages only)\n");
        printf("  --selftest                byte-identical double-emission check\n");
        return 1;
    }

    if (swarmDir)
    {
        memset(g_pher, 0, sizeof(g_pher));
        int nDep = 0, nForeign = 0;
        LoadSwarmState(swarmDir, &nDep, &nForeign);   // cold decode BEFORE bias turns on
        g_swarmOn = 1;
        printf("swarm: %d rated ids deposited (%d foreign-lineage skipped), lambda=%.2f -> lineage LXS1\n",
               nDep, nForeign, g_lambda);
        if (!nDep)
            printf("swarm: table is flat (no usable ratings) - this scan will equal a cold scan\n");
    }

    // ---- parallel screen (2026-07-18): seeds are independent, so workers
    // take contiguous chunks into private buffers. Concatenating the buffers
    // in worker order preserves ascending-seed order, and the DEDUP pass runs
    // sequentially over that merged order afterwards — byte-identical to the
    // old single-thread loop (first seed with a look-hash still wins).
    // GPU port considered and rejected: GPU float rounding varies by device/
    // driver, which would break the "same range = identical queue" contract.
    if (nThreads <= 0)
    {
        nThreads = (int)std::thread::hardware_concurrency() - 2;
        if (nThreads < 1) nThreads = 1;
    }
    if ((unsigned int)nThreads > count) nThreads = (int)count;

    std::vector<std::vector<Passer> > local((size_t)nThreads);
    std::vector<unsigned int> wGrown((size_t)nThreads, 0), wScreened((size_t)nThreads, 0);
    std::atomic<unsigned int> done(0);

    {
        std::vector<std::thread> pool;
        unsigned int chunk = count / (unsigned int)nThreads;
        for (int w = 0; w < nThreads; w++)
        {
            unsigned int s0 = start + chunk * (unsigned int)w;
            unsigned int s1 = (w == nThreads - 1) ? (start + count) : (s0 + chunk);
            pool.push_back(std::thread([&, w, s0, s1]()
            {
                std::vector<Passer>& out = local[(size_t)w];
                for (unsigned int seed = s0; seed < s1; seed++)
                {
                    Tree t;
                    float decay;
                    if (BuildFromSeed(seed, &t, &decay))
                    {
                        wGrown[(size_t)w]++;
                        ScreenResult r;
                        if (ScreenCandidate(&t, LensOfSeed(seed), decay, &r))
                        {
                            wScreened[(size_t)w]++;
                            Passer p;
                            p.seed  = seed;
                            p.score = r.score;
                            p.dance = r.dance;
                            p.nodes = t.count;
                            p.hash  = r.hash;
                            out.push_back(p);
                        }
                    }
                    if ((seed & 0xFFFF) == 0xFFFF)
                    {
                        unsigned int d = done.fetch_add(0x10000) + 0x10000;
                        if ((d & 0x3FFFF) == 0)             // every ~256K seeds
                        {
                            printf("  ...~%u/%u scanned\n", d, count);
                            // stdout is a pipe when the GUI runs us: without a
                            // flush the CRT holds these lines until exit and
                            // the progress bar has nothing to read.
                            fflush(stdout);
                        }
                    }
                }
            }));
        }
        for (size_t i = 0; i < pool.size(); i++) pool[i].join();
    }

    memset(g_dedup, 0, sizeof(g_dedup));
    unsigned int nGrown = 0, nScreened = 0, nDeduped = 0;
    std::vector<Passer> merged;
    for (int w = 0; w < nThreads; w++)
    {
        nGrown    += wGrown[(size_t)w];
        nScreened += wScreened[(size_t)w];
        for (size_t i = 0; i < local[(size_t)w].size(); i++)
        {
            if (DedupSeen(local[(size_t)w][i].hash)) { nDeduped++; continue; }
            merged.push_back(local[(size_t)w][i]);
        }
    }
    Passer* passers = merged.empty() ? NULL : &merged[0];
    int np = (int)merged.size();

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
            fprintf(q, "{\"id\":\"%s-%08X\",\"score\":%.5f,\"dance\":%.5f,\"nodes\":%d,\"hash\":\"%08X\",\"emitted\":%d}\n",
                    LineageStr(), passers[i].seed, passers[i].score, passers[i].dance,
                    passers[i].nodes, passers[i].hash, emit);
    }
    if (q) fclose(q);

    // Swarm runs archive their pheromone table: an LXS1 id regenerates only as
    // seed + this table (the .milk files stay the permanent artifact anyway).
    if (g_swarmOn)
    {
        char mpath[1024];
        _snprintf(mpath, sizeof(mpath) - 1, "%s\\swarm_manifest_%u_%u.txt", outdir, start, count);
        mpath[sizeof(mpath) - 1] = 0;
        FILE* m = fopen(mpath, "wb");
        if (m)
        {
            fprintf(m, "lineage LXS1  lambda %.4f  range [%u..%u)\n", g_lambda, start, start + count);
            fprintf(m, "# parent slot child weight (only nonzero entries)\n");
            for (int p = 0; p < 20; p++)
                for (int s = 0; s < 2; s++)
                    for (int k = 0; k < 19; k++)
                        if (g_pher[p][s][k] != 0.0f)
                            fprintf(m, "%d %d %d %.4f\n", p, s, k, g_pher[p][s][k]);
            fclose(m);
        }
    }

    printf("scan [%u..%u): %u grown, %u passed screens, %u dedup-dropped, %d queued, %d emitted -> %s (%d threads%s)\n",
           start, start + count, nGrown, nScreened, nDeduped, np, nEmitted, outdir, nThreads,
           g_swarmOn ? ", SWARM" : "");
    return 0;
}
