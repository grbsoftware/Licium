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
// LX4 ID CONTRACT (fixed spec — any change here bumps the genver again):
//   * seed: 32 bits, ID printed as LX%d-%08X (LX4).
//   * LX3 -> LX4 (2026-07-21, rung 2 "open scaffold"): the fixed analytic
//     medium (MediumOfSeed) is retired. A seed now grows TWO expressions and
//     the buf0 velocity is the curl of the second (the flow potential psi), so
//     the search reaches arbitrary flows and audio inside psi bends the motion.
//     Decay widened to F(0.0, 0.992) (pure-plot..heavy-fluid ingredient). Old
//     LX1/LX2/LX3 .milk files keep their own dressing and stay decodable-as-LXn.
//   * LX4 -> LX5 (2026-07-25, shaper curves): NK_CURVE joins the unary block as
//     a SOFT nonlinearity (the vocabulary had only hard floor/frac/abs, so every
//     roll cliffed). A NK_CURVE node samples a seed-baked shaping-curve LUT
//     (licium_curves.h). Curve seeds are SIDE-HASHES of the roll seed (never the
//     LCG), one per slot; a preset carries them as curveN_seed= lines. Slots 0..2
//     are tree-drawable; slot 3 is the comp color ramp (softens the fixed-scaffold
//     cliffs). Floor policy: hard steps are rationed (excess remapped to curves)
//     and screened (audio inside a hard step = strobe, rejected; a hard step
//     never appears without a soft curve present). Old LX1..LX4 files stay
//     decodable-as-LXn.
//   * RNG: LiciumRng::Seed32(seed) (licium_rng.h — 0x9E3779B9 ^ seed*2654435761).
//   * Draw order: (1) decay = F(0.0, 0.992); (2) the INK tree (root); (3) the
//     PSI tree (root2). Each tree: GrowNode does ONE class draw (Next16 % 100)
//     then ONE kind draw per node, children left -> right, depth-first. The
//     class draw happens even where the class is forced, so the stream never
//     depends on context. CONST draws its value as I(-20,20)/10.
//   * Node kind ids: the NodeKind enum below is frozen.
//   * Lens + palette are SIDE-HASHES of the seed (LensOfSeed / PaletteOfSeed),
//     never the LCG stream — the trees are seed-pure.
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
#include "../../vis_milk2/licium_curves.h"   // shaping-curve family (shared w/ engine)

#define LX_GENVER      6
#define LX_HARD_QUOTA  1        // LX5 floor policy: hard steps (floor/frac) allowed
                                // per BUILD before excess draws remap to soft curves
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
    // unary 7..11 (NK_CURVE is LX5's soft nonlinearity; the block stays contiguous
    // so BiasedPick(rng, NK_ABS, 5, ...) still spans exactly the unary kinds)
    NK_ABS = 7, NK_FRAC, NK_FLOOR, NK_NEG, NK_CURVE,
    // terminals 12..19
    NK_PX = 12, NK_PY, NK_TIME, NK_BASS, NK_MID, NK_TREB, NK_FB, NK_CONST
};

struct Node
{
    int   kind;
    float cval;     // NK_CONST only
    int   slot;     // NK_CURVE only: which curve LUT (0..2 drawable, never n->r —
                    // DepositNode recurses r into node indices, so slot is its own field)
    int   l, r;     // pool indices, -1 = none
};

// LX4 (rung 2, 2026-07-21): a seed now grows TWO expressions into one pool —
// root  = the ink field (WHERE ink appears), as in LX1..LX3;
// root2 = the flow POTENTIAL psi. The medium is no longer a fixed analytic
//         swirl (MediumOfSeed); the buf0 velocity is the curl of this evolved
//         potential, so the FORMULA moves pixels and audio in psi bends the
//         motion (not just brightness). This is Gary's "a shader is much more
//         dynamic than this" — the open-scaffold rung.
struct Tree
{
    Node nodes[LX_MAX_NODES];
    int  count;
    int  root;      // ink field
    int  root2;     // flow potential psi (LX4)
    int  hardCount; // LX5: floor/frac drawn so far this build (drives rationing)
    LiciumCurve curve[LICIUM_NUM_CURVES];  // LX5: the roll's baked shaping curves
                                           // (0..2 sampled by nodes, 3 = comp ramp)
};

// ---- swarm mode (Gary, 2026-07-18 evening: revive the heritage doc's ACO
// core — "build swarm into this and see what it does"; cold stays the default
// cadence). Pheromone table (parent kind, child slot, child kind) -> weight,
// derived STATELESS at scan start from latent_votes.txt + latent_verdicts.jsonl
// (like +3, latebloom +1.5, alive +1, frozen/black -1, blowout -1.5; pass
// votes deposit NOTHING — the stub-pile law). The bias only deflects the kind
// draw where pheromone exists: a flat context reduces to the EXACT cold
// arithmetic (same single Next16 consumed), so a swarm scan over an empty
// state equals the cold scan tree-for-tree. Lineage LXS<genver> (tracks the
// current grammar): regeneration = seed + the run's archived table manifest;
// the .milk file is the artifact.

#define SWARM_ROOT 20                    // parent id for the root draw (== kind count)
static float g_pher[21][2][20];          // [parent kind|ROOT][child slot][child kind]
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

    unsigned int wi[20], total = 0;
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

// LX5: a curve slot's seed is a SIDE-HASH of the roll seed, exactly like the
// palette/lens side-hashes — the shaping curve is fully determined by the roll,
// but rolling one must never advance the tree's LCG. Masked to 31 bits so it
// round-trips through the int-based curveN_seed= parse (state.cpp GetFastInt).
static unsigned int CurveSeedOfSlot(unsigned int seed, int slot)
{
    unsigned int h = (seed ^ (0x43555230u + (unsigned int)slot)) * 2246822519u;  // 'CUR0'+slot
    return h & 0x7FFFFFFFu;
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
    n->slot = 0;

    if (cls == 0)
    {
        n->kind = BiasedPick(rng, NK_ADD, 7, parent, slot);
        n->l = GrowNode(rng, t, depth + 1, n->kind, 0);
        n->r = GrowNode(rng, t, depth + 1, n->kind, 1);
        if (n->l < 0 || n->r < 0) return -1;
    }
    else if (cls == 1)
    {
        // Unary block is now 5 kinds (NK_CURVE joined). A directly-drawn NK_CURVE
        // draws its slot (a conditional extra draw, like NK_CONST's cval). Floor
        // policy (LX5): floor/frac are rationed — the first LX_HARD_QUOTA per build
        // stay hard, and every excess one is REMAPPED to a soft curve. Remapping
        // consumes NO extra draw (slot is derived from the node index), so the LCG
        // stream is identical to the un-rationed grammar; only the label changes.
        n->kind = BiasedPick(rng, NK_ABS, 5, parent, slot);
        if (n->kind == NK_CURVE)
            n->slot = rng->I(0, 2);
        else if (n->kind == NK_FLOOR || n->kind == NK_FRAC)
        {
            if (t->hardCount >= LX_HARD_QUOTA) { n->kind = NK_CURVE; n->slot = i % 3; }
            else t->hardCount++;
        }
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

// The whole deterministic derivation from a seed (LX4): decay, then the ink
// tree, then the flow-potential tree — fixed draw order so the stream is
// context-free. LX4 widens decay from a narrow fluid band to the full
// pure-plot..heavy-fluid ingredient range (Gary: "decay demoted from law to
// ingredient" — feedback is one costume, not the constitution).
static int BuildFromSeed(unsigned int seed, Tree* t, float* decay)
{
    LiciumRng rng;
    rng.Seed32(seed);
    *decay = rng.F(0.0f, 0.992f);
    t->count = 0;
    t->hardCount = 0;
    // LX5: bake the roll's four shaping curves from side-hash seeds (so EvalNode
    // can read them in the screens, and EmitPreset writes the same seeds). Slots
    // 0..2 are tree-drawable; slot 3 is the comp color ramp.
    for (int c = 0; c < LICIUM_NUM_CURVES; c++)
        t->curve[c] = LiciumCurveFromSeedFamily(CurveSeedOfSlot(seed, c), LICIUM_CURVE_FAMILY_CUR);
    t->root  = GrowNode(&rng, t, 0, SWARM_ROOT, 0);
    if (t->root < 0) return 0;
    t->root2 = GrowNode(&rng, t, 0, SWARM_ROOT, 0);   // psi flow potential
    return t->root2 >= 0;
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
// each rated CURRENT-lineage id is decoded (call this before g_swarmOn goes on)
// and its links deposited. Other lineages (older cold LX<n>, swarm LXS<n>) are
// skipped — their trees don't match the current grammar — counted and reported.
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

    // A rated id only regenerates its ACTUAL tree under the grammar it was grown
    // with. BuildFromSeed here is the CURRENT (LX<genver>) grammar, so only
    // current-lineage ids ("LX<genver>-") deposit correctly; older cold
    // lineages and swarm ids are foreign (their trees differ under this grammar)
    // and are skipped, counted, and reported. This is what makes swarm learn
    // from — and grow within — the version we are actually searching.
    char curpfx[8];
    _snprintf(curpfx, sizeof(curpfx) - 1, "LX%d-", LX_GENVER);
    curpfx[sizeof(curpfx) - 1] = 0;
    const size_t curpl = strlen(curpfx);

    *outDeposited = 0;
    *outForeign = 0;
    for (int i = 0; i < nr; i++)
    {
        float w = rated[i].voteW + rated[i].verdW;
        if (w == 0.0f) continue;
        if (strncmp(rated[i].id, curpfx, curpl) != 0) { (*outForeign)++; continue; }
        unsigned int seed = (unsigned int)strtoul(rated[i].id + curpl, NULL, 16);
        Tree t;
        float decay;
        if (!BuildFromSeed(seed, &t, &decay)) continue;
        DepositNode(&t, t.root,  SWARM_ROOT, 0, w);
        DepositNode(&t, t.root2, SWARM_ROOT, 0, w);   // ink + psi links both count
        (*outDeposited)++;
    }
}

// Lineage tag for filenames / queue ids. BOTH track the current grammar
// version: cold = LX<genver>, swarm = LXS<genver>. Swarm biases the SAME
// grammar cold uses (BiasedPick only deflects the kind draw), so a swarm roll
// is a current-genver tree — it must carry the current version, not a frozen
// "LXS1", or rolls grown under different grammars collide under one tag and no
// longer regenerate. (Gary, 2026-07-21: "swarm should work for any version we
// are currently on.")
static const char* LineageStr(void)
{
    static char cold[8]  = { 0 };
    static char swarm[8] = { 0 };
    if (g_swarmOn)
    {
        if (!swarm[0]) _snprintf(swarm, sizeof(swarm) - 1, "LXS%d", LX_GENVER);
        return swarm;
    }
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
    case NK_CURVE:return LiciumCurveEvalFamily(tr->curve[n->slot], EvalNode(tr, n->l, c));
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

// LX5: does any node sample curve slot `s`? (slot 3 = comp ramp is never a node
// slot, so it's driven separately in EmitPreset.)
static int TreeUsesCurveSlot(const Tree* t, int s)
{
    for (int i = 0; i < t->count; i++)
        if (t->nodes[i].kind == NK_CURVE && t->nodes[i].slot == s) return 1;
    return 0;
}

// Per-subtree walkers — the pool holds both the ink tree and psi, so the
// structural screens must ask about ONE root's subtree, not the whole pool.
static int SubtreeUses(const Tree* t, int i, int kindLo, int kindHi)
{
    if (i < 0) return 0;
    const Node* n = &t->nodes[i];
    if (n->kind >= kindLo && n->kind <= kindHi) return 1;
    return SubtreeUses(t, n->l, kindLo, kindHi) || SubtreeUses(t, n->r, kindLo, kindHi);
}
static int SubtreeSize(const Tree* t, int i)
{
    if (i < 0) return 0;
    const Node* n = &t->nodes[i];
    return 1 + SubtreeSize(t, n->l) + SubtreeSize(t, n->r);
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

// The curl-of-psi velocity at one point, exactly as the shader builds it
// (finite-difference gradient of root2, rotate 90, magnitude cap 3). Audio is
// injected the same way the engine does (bass=mid=treb=aud in steady state).
static void PsiVel(const Tree* t, float px, float py, float tt, float aud,
                   float* vx, float* vy)
{
    const float e = 0.012f;
    EvalCtx c; c.t = tt; c.bass = c.mid = c.treb = aud; c.fb = 0.0f;
    c.px = px + e; c.py = py; float pxp = EvalNode(t, t->root2, &c);
    c.px = px - e; c.py = py; float pxm = EvalNode(t, t->root2, &c);
    c.px = px; c.py = py + e; float pyp = EvalNode(t, t->root2, &c);
    c.px = px; c.py = py - e; float pym = EvalNode(t, t->root2, &c);
    float dpsidx = (pxp - pxm) * (1.0f / (2.0f * e));
    float dpsidy = (pyp - pym) * (1.0f / (2.0f * e));
    float ux = dpsidy, uy = -dpsidx;
    float vl = sqrtf(ux * ux + uy * uy);
    float s = 3.0f / (vl + 0.0001f); if (s > 1.0f) s = 1.0f;
    *vx = ux * s; *vy = uy * s;
}

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
    // 1. Budget — per subtree now (the pool holds ink + psi).
    int inkSz = SubtreeSize(t, t->root);
    int psiSz = SubtreeSize(t, t->root2);
    if (inkSz < LX_MIN_SIZE || inkSz > LX_MAX_SIZE) return 0;
    if (psiSz < 3           || psiSz > LX_MAX_SIZE) return 0;

    // 2. Structural must: the INK field uses position (else a flat wash).
    if (!SubtreeUses(t, t->root, NK_PX, NK_PY)) return 0;

    // 2b. LX5 floor policy (structural, cheap, both trees share the pool):
    //   - AUDIO INSIDE A HARD STEP IS THE STROBE. floor(...-bass_att) snaps the
    //     whole field across an integer boundary on the beat = the blinding
    //     full-field flash. Reject any floor/frac whose subtree pulls in audio.
    //     (Spatial-arg floors = stripes, fine; audio-arg floors = strobe, out.)
    //   - NEVER ALONE. A hard step with no soft curve anywhere is an un-eased
    //     cliff; rationing already injects curves past the quota, so this only
    //     culls the rare lone-floor tree.
    for (int i = 0; i < t->count; i++)
        if (t->nodes[i].kind == NK_FLOOR || t->nodes[i].kind == NK_FRAC)
            if (SubtreeUses(t, t->nodes[i].l, NK_BASS, NK_TREB)) return 0;
    if (TreeUses(t, NK_FRAC, NK_FLOOR) && !TreeUses(t, NK_CURVE, NK_CURVE)) return 0;

    // 3. The flow screen (LX4 rung 2 — this is the new must-dance). psi must
    //    produce a REAL flow (nonzero curl somewhere) that AUDIO actually bends.
    //    A structural "psi uses audio" check is not enough: audio that appears
    //    additively (psi = p.x - treb) cancels in the gradient. So measure the
    //    curl field numerically at two audio levels and require both motion and
    //    audio sensitivity.
    {
        const int G = 8;
        float flow = 0.0f, gdance = 0.0f;
        for (int gy = 0; gy < G; gy++)
        for (int gx = 0; gx < G; gx++)
        {
            float px = -1.0f + 2.0f * (gx + 0.5f) / G;
            float py = -1.0f + 2.0f * (gy + 0.5f) / G;
            float v0x, v0y, v1x, v1y;
            PsiVel(t, px, py, 0.5f, 1.00f, &v0x, &v0y);
            PsiVel(t, px, py, 0.5f, 1.35f, &v1x, &v1y);
            flow   += sqrtf(v0x * v0x + v0y * v0y);
            gdance += fabsf(v1x - v0x) + fabsf(v1y - v0y);
        }
        flow   /= (G * G);
        gdance /= (G * G);
        if (flow   < 0.12f) return 0;    // essentially still -> no motion
        if (gdance < 0.03f) return 0;    // audio doesn't bend the flow
    }

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

    // Ink audio-response (informational): audio now bends GEOMETRY, not the ink
    // field, so this no longer gates — it only rewards an ink field that also
    // happens to respond. The must-dance guarantee moved to the psi-uses-audio
    // structural screen above.
    SettleField(t, lens, decay, 32, 1.35f, 0.5f, f1);
    float dance = 0.0f;
    for (int i = 0; i < 32 * 32; i++) dance += fabsf(f1[i] - f0[i]);
    dance /= 32.0f * 32.0f;

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

// Position variable the emitted expression reads for p.x/p.y. Normally "p"
// (the pixel), but psi is emitted as a function of "q" so the shader can
// finite-difference it at neighbour offsets to form the curl velocity.
static const char* g_posVar = "p";

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
    case NK_CURVE:
        // CurveN() clamps its input (CurveUV saturates), matching LiciumCurveEval,
        // so the unbounded child expression is safe to feed raw.
        _snprintf(tmp, sizeof(tmp) - 1, "Curve%d(", n->slot); tmp[sizeof(tmp) - 1] = 0;
        s->Raw(tmp); EmitExpr(t, n->l, s); s->Raw(")");
        break;
    case NK_PX:  s->Raw(g_posVar); s->Raw(".x"); break;
    case NK_PY:  s->Raw(g_posVar); s->Raw(".y"); break;
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

// ---- rung 1 -> rung 2 (LX3 -> LX4) ------------------------------------------
// LX3 rolled the medium from a FIXED menu of 4 analytic swirls (MediumOfSeed).
// Gary's rung-2 critique ("a shader is much more dynamic than this; all the
// music is on brightness") retired that menu: LX4's medium is the curl of an
// EVOLVED flow potential (root2), so the search reaches arbitrary flows and
// audio bends them. MediumOfSeed is gone; the velocity is built in EmitPreset
// from psi. (LX1..LX3 .milk files keep their own fixed-medium dressing.)

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

    // LX5 shaping-curve seeds. Slots 0..2 are written only if the tree samples
    // them (0 = unused -> the engine bakes identity, so a stray Curve is a no-op);
    // slot 3 is the comp color ramp and is always dressed. Seeds are already
    // 31-bit (CurveSeedOfSlot masks), matching the int-based curveN_seed= parse.
    for (int cs = 0; cs < LICIUM_NUM_CURVES; cs++)
    {
        unsigned int cseed = 0;
        if (cs == 3)                        cseed = CurveSeedOfSlot(seed, 3);
        else if (TreeUsesCurveSlot(t, cs))  cseed = CurveSeedOfSlot(seed, cs);
        out->Add("curve%d_seed=%u", cs, cseed);
    }

    // LX6: declare which curve FAMILY those seeds are to be read under. Without
    // this key the engine defaults to V1 - which is exactly right for every roll
    // emitted before now, and exactly wrong for this one.
    out->Add("curve_family=%d", LICIUM_CURVE_FAMILY_CUR);

    // --- buf0: EVOLVED medium, evolved ink (LX4 rung 2) ----------------------
    // The template is the guardrail (decay, injection, clamp, velocity cap) but
    // the MOTION is no longer a fixed swirl: velocity = curl of an evolved flow
    // potential psi (root2), finite-differenced in the shader. Audio inside psi
    // bends the flow (screens require it), so music changes SHAPE, not just
    // brightness. The ink tree (root) still shapes WHERE ink appears. fb/tt/
    // audio are read once (center values) and shared across the psi neighbour
    // evals — psi is a spatial potential; per-pixel fb only tints it slightly.
    {
        Str body;
        body.Init();

        body.Raw("shader_body\n{\n");
        body.Raw("float2 p = (uv - 0.5) * float2(texsize.x*texsize.w, 1.0) * 2.0;\n");
        body.Raw("float tt = time * 0.25;\n");
        body.Raw("float3 prevc = tex2D(sampler_buf0, uv).xyz;\n");
        body.Raw("float fb = dot(prevc, float3(0.32, 0.49, 0.29));\n");
        body.Raw("float f = ");
        g_posVar = "p";
        EmitExpr(t, t->root, &body);
        body.Raw(";\n");
        if (lens == 0)      body.Raw("f = saturate(f);\n");
        else if (lens == 1) body.Raw("f = saturate(abs(f));\n");
        else                body.Raw("f = saturate(1.0 - abs(f));\n");

        // Evolved flow potential psi -> divergence-free curl velocity.
        body.Raw("float e = 0.012;\n");
        body.Raw("float2 pxp = p + float2(e,0.0);  float2 pxm = p - float2(e,0.0);\n");
        body.Raw("float2 pyp = p + float2(0.0,e);  float2 pym = p - float2(0.0,e);\n");
        body.Raw("float dpsidx = ((");
        g_posVar = "pxp"; EmitExpr(t, t->root2, &body);
        body.Raw(") - (");
        g_posVar = "pxm"; EmitExpr(t, t->root2, &body);
        body.Raw(")) * (1.0/(2.0*e));\n");
        body.Raw("float dpsidy = ((");
        g_posVar = "pyp"; EmitExpr(t, t->root2, &body);
        body.Raw(") - (");
        g_posVar = "pym"; EmitExpr(t, t->root2, &body);
        body.Raw(")) * (1.0/(2.0*e));\n");
        g_posVar = "p";
        body.Raw("float2 vel = float2(dpsidy, -dpsidx);\n");
        body.Raw("float vl = length(vel);\n");
        body.Raw("vel *= min(1.0, 3.0/(vl + 0.0001));\n");   // cap: no off-screen leaps
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
        // LX6: three separate causes of visible colour BANDING are fixed here.
        // Gary's report was "the flats are still there ... some of them lost the
        // flats and were replaced with thin lines for the colors", after the
        // fp16 LUT had already removed the 8-bit quantization plateaus. So the
        // remaining bands were being produced by this block, not by the table.
        //
        // 1. saturate() on the density input CLIPPED. Every pixel past the knee
        //    pinned to exactly 1.0 and shaded to pure accent - a genuine flat
        //    region, and the brighter the roll the larger it got. Replaced with
        //    1-exp(-x), which is monotone, smooth, and approaches 1 asymptotically
        //    without ever arriving: no value is ever repeated, so no plateau.
        //
        // 2. smoothstep has ZERO DERIVATIVE AT BOTH ENDPOINTS. Weighting the
        //    stops with smoothstep(0,0.34) / (0.33,0.67) / (0.66,1) meant the
        //    colour stopped changing at each stop boundary and at both extremes -
        //    a stall reads as a band exactly like a plateau does. The stops are
        //    now walked with a single parameter at CONSTANT slope, so the colour
        //    is always moving. A slope corner at a stop is far weaker than a
        //    stall, and interpolating in linear light (below) softens it further.
        //
        // 3. The lerp ran on gamma-encoded literals, which sags through a dark
        //    muddy middle between two stops and spaces the fade unevenly. The
        //    stops are emitted PRE-SQUARED so the whole chain interpolates in
        //    linear light, with one sqrt at the end to get back. That is not full
        //    oklab - it is the cheap 90% of it, for one instruction.
        body.Raw("float dens = Curve3(1.0 - exp(-max(dot(c, lw) * 3.0, 0.0)));\n");
        body.Raw("float rt = dens * 3.0;\n");
        _snprintf(l, sizeof(l) - 1,
            "float3 ramp = lerp(float3(%.4f,%.4f,%.4f), float3(%.4f,%.4f,%.4f), saturate(rt));\n",
            deep[0]*deep[0], deep[1]*deep[1], deep[2]*deep[2],
            s1[0]*s1[0],     s1[1]*s1[1],     s1[2]*s1[2]);
        l[sizeof(l) - 1] = 0; body.Raw(l);
        _snprintf(l, sizeof(l) - 1,
            "ramp = lerp(ramp, float3(%.4f,%.4f,%.4f), saturate(rt - 1.0));\n",
            s2[0]*s2[0], s2[1]*s2[1], s2[2]*s2[2]);
        l[sizeof(l) - 1] = 0; body.Raw(l);
        _snprintf(l, sizeof(l) - 1,
            "ramp = lerp(ramp, float3(%.4f,%.4f,%.4f), saturate(rt - 2.0));\n",
            accent[0]*accent[0], accent[1]*accent[1], accent[2]*accent[2]);
        l[sizeof(l) - 1] = 0; body.Raw(l);
        body.Raw("ramp = sqrt(max(ramp, 0.0));\n");
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
    if      (n->kind == NK_CONST) printf("%d(%.1f) ", n->kind, n->cval);
    else if (n->kind == NK_CURVE) printf("%d[c%d] ", n->kind, n->slot);
    else                          printf("%d ", n->kind);
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

// ===== Families (LXF trunk): the +1 graft ===================================
// A promoted (liked) tree seeds a NEW trunk. At every node SITE across both
// roots we graft exactly one node keeping the existing subtree, and enumerate
// EVERY such graft EXHAUSTIVELY - no RNG. The (site, variant) pair is a flat
// index: graftIdx = site * g_nGrafts + variant; id = (familyIdx<<16)|graftIdx,
// lineage LXF<genver>-. THE ENUMERATION ORDER BELOW IS A FROZEN CONTRACT -
// changing g_grafts or the site order re-labels every existing family roll (that
// is a family-version bump, tracked here, NOT a cold genver bump). Design:
// docs\SHADER_CURVES_DESIGN.md (Families section).

// Graft terminal choices: the 7 non-const terminals (NK_PX..NK_FB) + a fixed
// const ladder. The ladder is part of the frozen contract.
static const float LXF_CONST_LADDER[] = { -1.0f, 0.5f, 1.0f, 2.0f };
#define LXF_NCONST (int)(sizeof(LXF_CONST_LADDER) / sizeof(LXF_CONST_LADDER[0]))

static int LxfCommutative(int b) { return b == NK_ADD || b == NK_MUL || b == NK_MIN || b == NK_MAX; }

struct GraftDesc
{
    int   isBinary;   // 0 unary wrap u(T), 1 binary wrap b(T,term)/b(term,T)
    int   kind;       // unary kind, or binary kind
    int   slot;       // curve slot for a NK_CURVE unary
    int   termKind;   // binary only: the grafted terminal kind
    float termCval;   // binary only: const value if termKind == NK_CONST
    int   tLeft;      // binary only: 1 => b(T, term), 0 => b(term, T)
};
static GraftDesc g_grafts[256];
static int       g_nGrafts = 0;

// Build the frozen variant table once. Order: 7 unary wraps, then for each
// binary kind, each terminal choice (7 vars then the const ladder), each
// ordering (commutative binaries emit only T-left; others emit both).
static void BuildGraftTable(void)
{
    if (g_nGrafts) return;
    static const int uk[7] = { NK_ABS, NK_FRAC, NK_FLOOR, NK_NEG, NK_CURVE, NK_CURVE, NK_CURVE };
    static const int us[7] = { 0, 0, 0, 0, 0, 1, 2 };
    for (int i = 0; i < 7; i++)
    {
        GraftDesc d = { 0, uk[i], us[i], 0, 0.0f, 0 };
        g_grafts[g_nGrafts++] = d;
    }
    for (int b = NK_ADD; b <= NK_LT; b++)
        for (int tsel = 0; tsel < 7 + LXF_NCONST; tsel++)
        {
            int   termKind = (tsel < 7) ? (NK_PX + tsel) : NK_CONST;
            float cv       = (tsel < 7) ? 0.0f : LXF_CONST_LADDER[tsel - 7];
            int   orders   = LxfCommutative(b) ? 1 : 2;
            for (int o = 0; o < orders; o++)
            {
                GraftDesc d = { 1, b, 0, termKind, cv, (o == 0) ? 1 : 0 };
                g_grafts[g_nGrafts++] = d;
            }
        }
}

// Apply one graft (site index, variant) to a COPY of the family tree. Returns 0
// if the pool is full. The site's subtree is kept intact; its parent link (a
// root or a node's l/r - a tree has exactly one) is repointed at the wrap node.
static int ApplyGraft(const Tree* src, int site, const GraftDesc* g, Tree* dst)
{
    if (src->count >= LX_MAX_NODES - 2) return 0;
    *dst = *src;
    int orig = src->count;
    int wrap = dst->count++;
    Node* w = &dst->nodes[wrap];
    w->kind = g->kind; w->cval = 0.0f; w->slot = g->slot; w->l = -1; w->r = -1;
    if (!g->isBinary)
        w->l = site;                          // u(T)
    else
    {
        int term = dst->count++;
        Node* tn = &dst->nodes[term];
        tn->kind = g->termKind; tn->cval = g->termCval; tn->slot = 0; tn->l = -1; tn->r = -1;
        if (g->tLeft) { w->l = site; w->r = term; }   // b(T, term)
        else          { w->l = term; w->r = site; }   // b(term, T)
    }
    if      (dst->root  == site) dst->root  = wrap;
    else if (dst->root2 == site) dst->root2 = wrap;
    else
        for (int k = 0; k < orig; k++)        // parent is an original node
        {
            if (dst->nodes[k].l == site) { dst->nodes[k].l = wrap; break; }
            if (dst->nodes[k].r == site) { dst->nodes[k].r = wrap; break; }
        }
    return 1;
}

// ---- frozen family library (latent_families.txt) ---------------------------
// Append-only; a family's index is its position and is PERMANENT. Each record:
//   F <index> <originId> <seedHex> <decay> <root> <root2> <count>
//   N <kind> <cval> <slot> <l> <r>      (count lines)
// Curves re-derive from the stored origin seed at load (the same stable
// side-hash EmitPreset uses), so a family reproduces genver-independently.
struct Family
{
    char         originId[24];
    unsigned int seed;
    float        decay;
    Tree         tree;
};
static Family g_fam[4096];
static int    g_nFam = 0;

static void FamilyBakeCurves(Family* fm)
{
    for (int c = 0; c < LICIUM_NUM_CURVES; c++)
        fm->tree.curve[c] = LiciumCurveFromSeedFamily(CurveSeedOfSlot(fm->seed, c), LICIUM_CURVE_FAMILY_CUR);
}

static int LoadFamilies(const char* stateDir)
{
    g_nFam = 0;
    char path[1024];
    _snprintf(path, sizeof(path) - 1, "%s\\latent_families.txt", stateDir);
    path[sizeof(path) - 1] = 0;
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    char line[512];
    Family* cur = NULL;
    int nodeLeft = 0, ni = 0;
    while (fgets(line, sizeof(line), f))
    {
        if (line[0] == '#') continue;
        if (line[0] == 'F')
        {
            if (g_nFam >= 4096) break;
            cur = &g_fam[g_nFam++];
            int idx, root, root2, count; char oid[24]; unsigned int seed; double decay;
            if (sscanf(line, "F %d %23s %x %lf %d %d %d",
                       &idx, oid, &seed, &decay, &root, &root2, &count) == 7)
            {
                strncpy(cur->originId, oid, 23); cur->originId[23] = 0;
                cur->seed = seed; cur->decay = (float)decay;
                cur->tree.count = count; cur->tree.root = root; cur->tree.root2 = root2;
                cur->tree.hardCount = 0;
                nodeLeft = count; ni = 0;
            }
            else { g_nFam--; cur = NULL; nodeLeft = 0; }
        }
        else if (line[0] == 'N' && cur && nodeLeft > 0)
        {
            int kind, slot, l, r; double cval;
            if (sscanf(line, "N %d %lf %d %d %d", &kind, &cval, &slot, &l, &r) == 5 && ni < LX_MAX_NODES)
            {
                Node* n = &cur->tree.nodes[ni++];
                n->kind = kind; n->cval = (float)cval; n->slot = slot; n->l = l; n->r = r;
                nodeLeft--;
            }
        }
    }
    fclose(f);
    for (int i = 0; i < g_nFam; i++) FamilyBakeCurves(&g_fam[i]);
    return g_nFam;
}

static int FamilyKnown(const char* originId)
{
    for (int i = 0; i < g_nFam; i++)
        if (!strcmp(g_fam[i].originId, originId)) return 1;
    return 0;
}

static void AppendFamilyRecord(const char* stateDir, int index, const char* originId,
                               unsigned int seed, float decay, const Tree* t)
{
    char path[1024];
    _snprintf(path, sizeof(path) - 1, "%s\\latent_families.txt", stateDir);
    path[sizeof(path) - 1] = 0;
    int fresh = 1;
    { FILE* c = fopen(path, "rb"); if (c) { fresh = 0; fclose(c); } }
    FILE* f = fopen(path, "ab");
    if (!f) { printf("promote: cannot open %s\n", path); return; }
    if (fresh)
        fprintf(f, "# latent families - append-only; index = record order; DO NOT reorder or delete.\n");
    fprintf(f, "F %d %s %08X %.6f %d %d %d\n",
            index, originId, seed, decay, t->root, t->root2, t->count);
    for (int i = 0; i < t->count; i++)
        fprintf(f, "N %d %.4f %d %d %d\n",
                t->nodes[i].kind, t->nodes[i].cval, t->nodes[i].slot, t->nodes[i].l, t->nodes[i].r);
    fclose(f);
}

// --promote STATEDIR: for each id currently flagged in latent_promote.txt
// (later-wins) that isn't already a family, rebuild its tree from the seed under
// the CURRENT grammar and freeze it into latent_families.txt. Foreign-genver ids
// are refused - a seed only rebuilds its real tree under the grammar that made it.
static int PromoteFromState(const char* stateDir)
{
    BuildGraftTable();
    LoadFamilies(stateDir);   // seeds g_nFam so appends continue the index
    int startIdx = g_nFam;

    // later-wins set of currently-flagged ids
    struct Flag { char id[24]; int on; };
    static Flag flags[2048];
    int nf = 0;
    char path[1024], line[512];
    _snprintf(path, sizeof(path) - 1, "%s\\latent_promote.txt", stateDir);
    path[sizeof(path) - 1] = 0;
    FILE* f = fopen(path, "rb");
    if (!f) { printf("promote: no latent_promote.txt in %s (press 'f' in X first)\n", stateDir); return 0; }
    char id[24]; int v;
    while (fgets(line, sizeof(line), f))
        if (sscanf(line, "%23s %d", id, &v) == 2)
        {
            int j = -1;
            for (int i = 0; i < nf; i++) if (!strcmp(flags[i].id, id)) { j = i; break; }
            if (j < 0 && nf < 2048) { j = nf++; strncpy(flags[j].id, id, 23); flags[j].id[23] = 0; }
            if (j >= 0) flags[j].on = v;   // later lines win
        }
    fclose(f);

    int added = 0, skipForeign = 0, skipDup = 0, skipGrow = 0;
    for (int i = 0; i < nf; i++)
    {
        if (!flags[i].on) continue;
        if (FamilyKnown(flags[i].id)) { skipDup++; continue; }

        Tree t; float decay; unsigned int seed;
        int gen;

        // ROUND 2+ : the origin is itself a family roll, "LXF<gen>-<packed>".
        // A round-N family is just another append-only library entry whose
        // origin happens to be a graft rather than a cold seed - so depth is a
        // LIBRARY attribute and the id format never changes (the alternative,
        // an explicit round counter in the id, would have cost another
        // LatParseId migration for nothing). Its tree is the parent's tree with
        // the parent's graft already applied, so the next --families run grafts
        // on top of that: +1 becomes +2, then +3, as deep as rolls keep earning
        // an f. Palette, curves and decay are INHERITED from the parent family
        // (they derive from the original cold seed), which is what keeps a
        // lineage looking like itself as it deepens.
        if (sscanf(flags[i].id, "LXF%d-%x", &gen, &seed) == 2)
        {
            if (gen != LX_GENVER)
            {
                printf("promote: skip %s (genver %d != current %d - rebuild grammar mismatch)\n",
                       flags[i].id, gen, LX_GENVER);
                skipForeign++; continue;
            }
            int famIdx  = (int)(seed >> 16);
            int graftIdx= (int)(seed & 0xFFFF);
            if (famIdx < 0 || famIdx >= g_nFam || g_nGrafts <= 0)
            {
                printf("promote: skip %s (family %d not in the library - promote its parent first)\n",
                       flags[i].id, famIdx);
                skipForeign++; continue;
            }
            int site    = graftIdx / g_nGrafts;
            int variant = graftIdx % g_nGrafts;
            Family* parent = &g_fam[famIdx];
            if (variant >= g_nGrafts || site >= parent->tree.count)
            {
                printf("promote: skip %s (graft %d out of range for family %d)\n",
                       flags[i].id, graftIdx, famIdx);
                skipForeign++; continue;
            }
            if (!ApplyGraft(&parent->tree, site, &g_grafts[variant], &t))
            {
                skipGrow++; continue;
            }
            seed  = parent->seed;      // palette + curves stay the lineage's own
            decay = parent->decay;
        }
        // ROUND 1: a cold winner. The seed rebuilds the tree, so the grammar
        // that made it has to be the grammar running now.
        else if (sscanf(flags[i].id, "LX%d-%x", &gen, &seed) == 2)
        {
            if (gen != LX_GENVER)
            {
                printf("promote: skip %s (genver %d != current %d - rebuild grammar mismatch)\n",
                       flags[i].id, gen, LX_GENVER);
                skipForeign++; continue;
            }
            if (!BuildFromSeed(seed, &t, &decay)) { skipGrow++; continue; }
        }
        else
        {
            printf("promote: skip %s (not a cold LX or family LXF id)\n", flags[i].id);
            skipForeign++; continue;
        }

        int idx = startIdx + added;
        AppendFamilyRecord(stateDir, idx, flags[i].id, seed, decay, &t);

        // Register in memory too, so a chain promoted in ONE run resolves: a
        // round-3 id flagged alongside its round-2 parent needs that parent to
        // be findable, and LoadFamilies only ran before the loop.
        if (g_nFam < 4096 && idx == g_nFam)
        {
            Family* fm = &g_fam[g_nFam++];
            strncpy(fm->originId, flags[i].id, 23); fm->originId[23] = 0;
            fm->seed  = seed;
            fm->decay = decay;
            fm->tree  = t;
            FamilyBakeCurves(fm);
        }

        printf("promote: +family %d <- %s (%d nodes, ink %d / psi %d)\n",
               idx, flags[i].id, t.count,
               SubtreeSize(&t, t.root), SubtreeSize(&t, t.root2));
        added++;
    }
    printf("promote: %d added, %d already-known, %d foreign/bad, %d grow-fail -> %d families total\n",
           added, skipDup, skipForeign, skipGrow, startIdx + added);
    return added;
}

// --families STATEDIR --outdir DIR: exhaustively graft every promoted family,
// screen, and emit a stratified-by-site slice as LXF<gen>- rolls next to the
// cold haul. Deterministic (no RNG) - same library + code => byte-identical rolls.
struct FamPasser { int fam; int site; int variant; unsigned int packed; float score; };

static int CmpFamStratify(const void* a, const void* b)
{
    const FamPasser* pa = (const FamPasser*)a;
    const FamPasser* pb = (const FamPasser*)b;
    if (pa->fam  != pb->fam ) return pa->fam  - pb->fam;    // group by family
    if (pa->site != pb->site) return pa->site - pb->site;   // then by graft site
    if (pa->score > pb->score) return -1;                   // best first within a site
    if (pa->score < pb->score) return 1;
    return (int)pa->packed - (int)pb->packed;               // stable
}

// One emit bucket = all passers grafted at the same (family, site). `ord` is the
// bucket's index WITHIN its family, and the walk is ordered by it so families
// interleave: site-0 of every family, then site-1 of every family, and so on.
struct FamBucket { int start, end, fam, ord; };

static int CmpFamBucket(const void* a, const void* b)
{
    const FamBucket* pa = (const FamBucket*)a;
    const FamBucket* pb = (const FamBucket*)b;
    if (pa->ord != pb->ord) return pa->ord - pb->ord;       // round-robin families
    if (pa->fam != pb->fam) return pa->fam - pb->fam;       // then library order
    return pa->start - pb->start;                           // stable (site order)
}

static int EmitFamilyRoll(const Family* fm, const Tree* graft, unsigned int packed,
                          const char* outdir)
{
    Str s; s.Init();
    if (!s.buf) return 0;
    EmitPreset(fm->seed, graft, fm->decay, &s);   // origin seed -> winner's palette+curves
    char path[1024];
    _snprintf(path, sizeof(path) - 1, "%s\\latent_LXF%d-%08X.milk", outdir, LX_GENVER, packed);
    path[sizeof(path) - 1] = 0;
    int ok = WriteBytes(path, s.buf, s.len);
    s.Free();
    return ok;
}

static int FamilyScan(const char* stateDir, const char* outdir, int emitCap)
{
    BuildGraftTable();
    int nfam = LoadFamilies(stateDir);
    if (!nfam) { printf("families: latent_families.txt empty or missing in %s (run --promote first)\n", stateDir); return 1; }
    printf("families: %d promoted, %d graft variants/site, lineage LXF%d\n", nfam, g_nGrafts, LX_GENVER);

    // Screen every graft in parallel. Work units = (family, site) pairs, each
    // expanding to g_nGrafts screens; workers take contiguous unit ranges into
    // private buffers (family trees are read-only). Order is irrelevant here -
    // the stratify sort below is a total order, so the emitted slice is
    // deterministic regardless of thread scheduling.
    struct Unit { int fam; int site; };
    std::vector<Unit> units;
    for (int fi = 0; fi < nfam; fi++)
        for (int si = 0; si < g_fam[fi].tree.count; si++)   // pool order = both roots' preorder
        {
            Unit u; u.fam = fi; u.site = si; units.push_back(u);
        }
    unsigned int totalGrafts = (unsigned int)units.size() * (unsigned int)g_nGrafts;

    int nThreads = (int)std::thread::hardware_concurrency() - 2;
    if (nThreads < 1) nThreads = 1;
    if ((int)units.size() < nThreads) nThreads = (int)units.size();
    if (nThreads < 1) nThreads = 1;

    std::vector<std::vector<FamPasser> > localP((size_t)nThreads);
    std::atomic<unsigned int> graftDone(0), screenPass(0);

    {
        std::vector<std::thread> pool;
        int nu = (int)units.size();
        int chunk = nu / nThreads;
        for (int w = 0; w < nThreads; w++)
        {
            int u0 = chunk * w;
            int u1 = (w == nThreads - 1) ? nu : (u0 + chunk);
            pool.push_back(std::thread([&, w, u0, u1]()
            {
                std::vector<FamPasser>& out = localP[(size_t)w];
                for (int ui = u0; ui < u1; ui++)
                {
                    int fi = units[(size_t)ui].fam, si = units[(size_t)ui].site;
                    Family* fm = &g_fam[fi];
                    for (int vi = 0; vi < g_nGrafts; vi++)
                    {
                        Tree g;
                        if (!ApplyGraft(&fm->tree, si, &g_grafts[vi], &g)) continue;
                        for (int c = 0; c < LICIUM_NUM_CURVES; c++) g.curve[c] = fm->tree.curve[c];
                        g.hardCount = 0;
                        ScreenResult r;
                        int ok = ScreenCandidate(&g, LensOfSeed(fm->seed), fm->decay, &r);
                        unsigned int gd = graftDone.fetch_add(1) + 1;
                        if (ok)
                        {
                            screenPass.fetch_add(1);
                            FamPasser p;
                            p.fam = fi; p.site = si; p.variant = vi;
                            p.packed = ((unsigned int)fi << 16) | (unsigned int)(si * g_nGrafts + vi);
                            p.score = r.score;
                            out.push_back(p);
                        }
                        if ((gd & 0xFFF) == 0)   // ~every 4096 grafts
                        {
                            printf("  ...%u/%u grafts, %u passed\n", gd, totalGrafts, screenPass.load());
                            fflush(stdout);
                        }
                    }
                }
            }));
        }
        for (size_t i = 0; i < pool.size(); i++) pool[i].join();
    }

    std::vector<FamPasser> pass;
    for (int w = 0; w < nThreads; w++)
        for (size_t i = 0; i < localP[(size_t)w].size(); i++)
            pass.push_back(localP[(size_t)w][i]);
    unsigned int nGrafted = graftDone.load(), nScreened = screenPass.load();

    // Stratify by (family, site) round-robin: sort, then walk sites in rounds,
    // taking each site's best-remaining until the emit cap is hit. Maximises
    // "what happens if I add something HERE" spread over one grading session.
    FamPasser* pp = pass.empty() ? NULL : &pass[0];
    int npass = (int)pass.size();
    qsort(pp, npass, sizeof(FamPasser), CmpFamStratify);

    char qpath[1024];
    _snprintf(qpath, sizeof(qpath) - 1, "%s\\queue.jsonl", outdir);
    qpath[sizeof(qpath) - 1] = 0;
    FILE* q = fopen(qpath, "ab");

    // Cut the sorted passers into (family, site) buckets, number each bucket
    // within its family, and order the walk by that number. Walking in raw sort
    // order is family-major, so the emit cap is spent entirely on the lowest
    // family indices — a 7-family library at --emit 70 emitted families 0 and 1
    // and nothing else. Interleaved, an emit cap of N spreads over all families.
    std::vector<FamBucket> buckets;
    for (int i = 0; i < npass; )
    {
        int j = i + 1;
        while (j < npass && pp[j].fam == pp[i].fam && pp[j].site == pp[i].site) j++;
        FamBucket b; b.start = i; b.end = j; b.fam = pp[i].fam; b.ord = 0;
        buckets.push_back(b);
        i = j;
    }
    int nBuckets = (int)buckets.size();
    FamBucket* bb = buckets.empty() ? NULL : &buckets[0];
    {
        std::vector<int> nSeen((size_t)(g_nFam > 0 ? g_nFam : 1), 0);
        for (int b = 0; b < nBuckets; b++) bb[b].ord = nSeen[(size_t)bb[b].fam]++;
    }
    qsort(bb, nBuckets, sizeof(FamBucket), CmpFamBucket);

    std::vector<char> used((size_t)npass, 0);
    int nEmitted = 0;
    int anyLeft = 1;
    while (anyLeft && nEmitted < emitCap)
    {
        anyLeft = 0;
        for (int b = 0; b < nBuckets && nEmitted < emitCap; b++)
        {
            // take this bucket's best not-yet-used (they're score-sorted within)
            for (int k = bb[b].start; k < bb[b].end; k++)
                if (!used[(size_t)k])
                {
                    used[(size_t)k] = 1;
                    anyLeft = 1;
                    Family* fm = &g_fam[pp[k].fam];
                    Tree g;
                    if (ApplyGraft(&fm->tree, pp[k].site, &g_grafts[pp[k].variant], &g))
                    {
                        for (int c = 0; c < LICIUM_NUM_CURVES; c++) g.curve[c] = fm->tree.curve[c];
                        g.hardCount = 0;
                        if (EmitFamilyRoll(fm, &g, pp[k].packed, outdir))
                        {
                            nEmitted++;
                            if (q)
                                fprintf(q, "{\"id\":\"LXF%d-%08X\",\"score\":%.5f,\"family\":%d,\"site\":%d,\"emitted\":1}\n",
                                        LX_GENVER, pp[k].packed, pp[k].score, pp[k].fam, pp[k].site);
                        }
                    }
                    break;   // one per bucket per round
                }
        }
    }
    if (q) fclose(q);

    printf("families: %u grafts, %u passed screens, %d emitted -> %s (%d threads)\n",
           nGrafted, nScreened, nEmitted, outdir, nThreads);
    return 0;
}

int main(int argc, char** argv)
{
    unsigned int start = 0, count = 0, single = 0;
    int haveSingle = 0, haveDecode = 0, emitCap = LX_EMIT_CAP;
    int nThreads = 0;                  // 0 = auto (cores - 2, min 1)
    const char* outdir = NULL;
    const char* swarmDir = NULL;       // --swarm <Licium state dir> = LXS1 mode
    const char* outfile = NULL;        // --outfile PATH: exact path for --seed
    const char* promoteDir = NULL;     // --promote <state dir>: freeze flagged rolls
    const char* familiesDir = NULL;    // --families <state dir>: +1-graft scan

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
        else if (!strcmp(argv[i], "--promote")  && i + 1 < argc) promoteDir  = argv[++i];
        else if (!strcmp(argv[i], "--families") && i + 1 < argc) familiesDir = argv[++i];
        else { printf("unknown arg: %s\n", argv[i]); return 1; }
    }

    if (promoteDir)
        return PromoteFromState(promoteDir) >= 0 ? 0 : 1;

    if (familiesDir)
    {
        if (!outdir) { printf("--families needs --outdir DIR\n"); return 1; }
        return FamilyScan(familiesDir, outdir, emitCap);
    }

    if (haveDecode)
    {
        Tree t;
        float decay;
        if (!BuildFromSeed(single, &t, &decay)) { printf("grow failed (pool bound)\n"); return 1; }
        printf("LX%d-%08X\n", LX_GENVER, single);
        printf("  decay = %.5f  lens = %d  palette = %d  nodes = %d (ink %d / psi %d)\n",
               decay, LensOfSeed(single), PaletteOfSeed(single), t.count,
               SubtreeSize(&t, t.root), SubtreeSize(&t, t.root2));
        printf("  ink chain: ");
        PrintChain(&t, t.root);
        printf("\n  psi chain: ");
        PrintChain(&t, t.root2);
        printf("\n");
        {
            Str s; s.Init();
            g_posVar = "p"; EmitExpr(&t, t.root, &s);
            printf("  f   = %s\n", s.buf ? s.buf : "?");
            s.len = 0; if (s.buf) s.buf[0] = 0;
            EmitExpr(&t, t.root2, &s);
            printf("  psi = %s\n", s.buf ? s.buf : "?");
            s.Free();
        }
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
        printf("LX%d-%08X  decay=%.4f lens=%d  ink=%d psi=%d nodes  screen=%s",
               LX_GENVER, single, decay, LensOfSeed(single),
               SubtreeSize(&t, t.root), SubtreeSize(&t, t.root2),
               pass ? "PASS" : "reject");
        if (pass) printf("  score=%.4f dance=%.4f", r.score, r.dance);
        printf("\n  emitted (forced): %s\n", path);
        return 0;
    }

    if (!count || !outdir)
    {
        printf("latent_prospect (LX%d) — Latent Phase A. docs\\LATENT_DESIGN.md\n", LX_GENVER);
        printf("  --scan:     --start N --count N --outdir DIR [--emit K (default %d)] [--threads N (default cores-2)]\n", LX_EMIT_CAP);
        printf("  --swarm STATEDIR [--lambda F (default 0.35)]   ACO mode: pheromone from\n");
        printf("              current-lineage votes+verdicts biases growth; lineage LXS%d;\n", LX_GENVER);
        printf("              table manifest archived next to the queue\n");
        printf("  --seek threads            rank-side profile: surface filament/thread\n");
        printf("              visuals (ridge-rich, blob-poor fields); combinable with --swarm\n");
        printf("  --promote STATEDIR        freeze rolls flagged in latent_promote.txt ('f' in X)\n");
        printf("              into latent_families.txt (append-only, cold LX%d ids only)\n", LX_GENVER);
        printf("  --families STATEDIR --outdir DIR [--emit K]   +1-graft every promoted\n");
        printf("              family exhaustively, screen, stratify-by-site emit as LXF%d- rolls\n", LX_GENVER);
        printf("  --seed HEX --outdir DIR   screen + force-emit one seed\n");
        printf("  --decode HEX              print chain + formula (cold lineages only)\n");
        printf("  --selftest                byte-identical double-emission check\n");
        return 1;
    }

    if (swarmDir)
    {
        memset(g_pher, 0, sizeof(g_pher));
        int nDep = 0, nForeign = 0;
        LoadSwarmState(swarmDir, &nDep, &nForeign);   // decode BEFORE bias turns on
        g_swarmOn = 1;
        printf("swarm: %d rated ids deposited (%d foreign-lineage skipped), lambda=%.2f -> lineage %s\n",
               nDep, nForeign, g_lambda, LineageStr());
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

    // Swarm runs archive their pheromone table: a swarm id regenerates only as
    // seed + this table (the .milk files stay the permanent artifact anyway).
    if (g_swarmOn)
    {
        char mpath[1024];
        _snprintf(mpath, sizeof(mpath) - 1, "%s\\swarm_manifest_%u_%u.txt", outdir, start, count);
        mpath[sizeof(mpath) - 1] = 0;
        FILE* m = fopen(mpath, "wb");
        if (m)
        {
            fprintf(m, "lineage %s  lambda %.4f  range [%u..%u)\n", LineageStr(), g_lambda, start, start + count);
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
