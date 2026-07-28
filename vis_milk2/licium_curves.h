// licium_curves.h — Licium's shared shaping-curve family.
//
// A shaping curve maps a signal x in [0,1] to y in [0,1]. It exists because the
// prospector's vocabulary had ONLY hard nonlinearities (floor/frac/abs/neg) and
// therefore could not make a soft ramp — every roll cliffed, because cliffs were
// the only tool. See docs\SHADER_CURVES_DESIGN.md.
//
// FIXED SPEC, same standing as licium_rng.h: a curve is fully determined by its
// seed, and the seed is what a preset carries. Changing the math here changes
// what every existing curve-seed means, so it costs a GENVER bump of every tool
// that rolls one.
//
// Shared by the ENGINE (bakes the seed into a 256-entry LUT texture the shader
// samples) and the PROSPECTOR (evaluates the same curve on the CPU inside its
// screens). Both must agree, so this header depends on nothing but <math.h>.
//
// The family — two axes spanning "hard step" through "soft ease" to "bounce":
//
//   y0(x) = normalized tanh(k*(x-pivot))     the SHARPNESS axis. k~0.05 is a
//           straight line; k~24 is very nearly floor(). tanh is exactly the
//           soft nonlinearity the vocabulary was missing.
//
//   y(x)  = y0(x) + tension*0.4*sin(2*pi*lobes*x)*env(x)      the TENSION axis.
//           env(x) = 4x(1-x) vanishes at both ends, so y(0)=0 and y(1)=1 stay
//           pinned however wild the ripple gets. Monotone eases (smoothstep,
//           gamma) CANNOT overshoot; this term is what lets a curve spring past
//           rest and come back, which was a hard requirement.
//
// floor, smoothstep and a ball-bounce are three coordinates in that space.
//
// Out-of-range excursions are CLAMPED, not renormalized: clamping keeps the
// anchors exact (0->0, 1->1) and costs only a plateau, and a plateau is soft —
// its slope is zero. Renormalizing would preserve the overshoot but drag y(0)
// off zero, which would stop a dead signal from reading as dark.

#ifndef LICIUM_CURVES_H
#define LICIUM_CURVES_H

#include <math.h>
#include "licium_rng.h"

// Entry count of a baked 1-D curve table. 256 is chosen to match the engine's
// 8-bit buffers (PassBuffers_Ensure allocates D3DFMT_A8R8G8B8), not because
// longer tables are costly — a 4096-entry table would still be 16KB. The real
// limits on LUT growth are sampler slots and cache coherence, never size.
#define LICIUM_CURVE_LUT_W 256

// Number of curve slots a preset may bind (sampler_curve0 .. sampler_curve3).
// Mirrors NUM_PASS_BUFS; ps_3_0 offers 16 sampler slots and buf0..3 spend four.
#define LICIUM_NUM_CURVES 4

#ifndef LICIUM_PI
#define LICIUM_PI 3.14159265358979323846f
#endif

// Curve FAMILY VERSION. A preset carries one (curve_family= in the .milk,
// absent means 1), and the engine keeps every version forever, because the
// alternative is that improving the math silently repaints every roll already
// graded under the old math. v1 rolls must look like v1 rolls in ten years.
//
//   1 - tanh sharpness + clamped sine ripple. Ships in LX1..LX5.
//   2 - curvilinear: slope never dies and the ripple never hits a rail, so a
//       baked table has no flat run anywhere. Gary 2026-07-27: "I don't want to
//       see any flats that I didn't personally create. I want curvilinear
//       results." Measurement that drove it: under v1, 69.6% of curves held a
//       flat run of 8+ table cells and 53.8% held 24+ — about half of that from
//       the ripple clamping into the rails, the rest from tanh SATURATING,
//       which is structural to v1's shape and could not be tuned away.
#define LICIUM_CURVE_FAMILY_V1  1
#define LICIUM_CURVE_FAMILY_V2  2
#define LICIUM_CURVE_FAMILY_CUR LICIUM_CURVE_FAMILY_V2

// The floor v2 guarantees, as a fraction of the step a straight line takes
// between adjacent table cells.
//
// It is set against the QUANTIZED table, which is the only one that matters:
// the engine uploads 8 bits per entry, a straight line moves exactly 1/255 per
// cell, and so a curve moving at fraction f of linear repeats each 8-bit value
// for about 1/f cells. At 0.15 that is a seven-cell plateau in what the GPU
// samples however smooth the float table looks.
//
// Note the hard ceiling this works against: the mean step of ANY curve from 0
// to 1 is exactly one linear step, so the shallowest step can never exceed
// linear — a floor of 1.0 would admit only the straight line itself. 0.35 caps
// the worst quantized run at about three cells, roughly 1% of the domain,
// which is below the width anything reads as held-still.
#define LICIUM_CURVE_MIN_SLOPE 0.35f

struct LiciumCurve
{
    float k;        // sharpness: 0.05 (linear) .. ~24 (near-step)
    float pivot;    // where the S is centered, 0.30 .. 0.70
    float tension;  // overshoot amount, -1 .. 1 (0 = monotone ease)
    int   lobes;    // ripple lobes across the span, 1 .. 3
    float reach;    // v2 only: how far toward the pure tanh the shape leans.
                    // Capped below 1, and THAT is the no-flat guarantee: the
                    // residual (1-reach) of the identity keeps a slope alive
                    // everywhere the tanh has gone flat.
    int   family;   // which math above
};

// Seed -> parameters. The salt keeps this stream independent of the tree LCG,
// exactly as the medium/palette side-hashes do: the same seed must always grow
// the same formula, so a curve roll must never advance the tree's generator.
inline LiciumCurve LiciumCurveFromSeed(unsigned int seed)
{
    LiciumRng rng;
    rng.Seed32(seed ^ 0x43525631u);   // 'CRV1'

    LiciumCurve c;

    // Cubic on the sharpness draw biases the population toward GENTLE curves
    // while leaving the near-step extreme reachable. Softening the output is
    // the entire point of the facility, so soft is the common case.
    float u = rng.F(0.0f, 1.0f);
    c.k = 0.05f + 24.0f * u * u * u;

    c.pivot = rng.F(0.30f, 0.70f);

    // A third of curves are pure monotone eases. Without this, every curve
    // would ripple, and a plain soft ramp — the thing most rolls actually need
    // — would never be drawn.
    c.tension = (rng.I(0, 99) < 33) ? 0.0f : rng.F(-1.0f, 1.0f);
    c.lobes   = rng.I(1, 3);

    c.reach   = 1.0f;                       // v1 is the pure tanh
    c.family  = LICIUM_CURVE_FAMILY_V1;

    return c;
}

// v2 parameters. Salted apart from v1 so the two families are independent
// draws rather than one being a perturbation of the other — a v2 roll should
// be its own thing, not v1 with the corners knocked off.
inline LiciumCurve LiciumCurveFromSeedV2(unsigned int seed)
{
    LiciumRng rng;
    rng.Seed32(seed ^ 0x43525632u);   // 'CRV2'

    LiciumCurve c;

    float u = rng.F(0.0f, 1.0f);
    c.k = 0.05f + 24.0f * u * u * u;   // same cubic bias toward gentle
    c.pivot = rng.F(0.30f, 0.70f);

    // Reach is what makes v2 curvilinear. At 0.85 the shape is nearly the full
    // tanh but still carries 15% of a straight line's slope through the
    // saturated ends; at 0.35 it is a lazy bend. Drawn with a square-root bias
    // so the population favours the SHAPED end - low reach is available but
    // most curves should still be doing something.
    float r = rng.F(0.0f, 1.0f);
    c.reach = 0.35f + 0.50f * (float)sqrt((double)r);

    // Same one-in-three monotone rule as v1: a plain soft ramp is the thing
    // most rolls actually need, and without this every curve would ripple.
    c.tension = (rng.I(0, 99) < 33) ? 0.0f : rng.F(-1.0f, 1.0f);
    c.lobes   = rng.I(1, 3);

    c.family  = LICIUM_CURVE_FAMILY_V2;

    return c;
}

inline LiciumCurve LiciumCurveFromSeedFamily(unsigned int seed, int family)
{
    return (family >= LICIUM_CURVE_FAMILY_V2) ? LiciumCurveFromSeedV2(seed)
                                              : LiciumCurveFromSeed(seed);
}

// Evaluate the curve. x outside [0,1] is clamped, so a caller feeding an
// unbounded signal gets the endpoint value rather than nonsense — this matches
// what the shader's CLAMP sampler does at the texture edges.
inline float LiciumCurveEval(const LiciumCurve& c, float x)
{
    if (x < 0.0f) x = 0.0f;
    if (x > 1.0f) x = 1.0f;

    // Normalize tanh across [0,1] so the curve always spans its full range,
    // whatever the sharpness and pivot are.
    float a = tanhf(c.k * (0.0f - c.pivot));
    float b = tanhf(c.k * (1.0f - c.pivot));
    float d = b - a;
    float y = (d > 1e-6f || d < -1e-6f) ? (tanhf(c.k * (x - c.pivot)) - a) / d : x;

    if (c.tension != 0.0f)
    {
        float env = 4.0f * x * (1.0f - x);          // 0 at both ends, 1 at center
        y += c.tension * 0.4f * sinf(2.0f * LICIUM_PI * c.lobes * x) * env;
    }

    if (y < 0.0f) y = 0.0f;
    if (y > 1.0f) y = 1.0f;
    return y;
}

// v2. Two changes from v1, each aimed at one of the two sources of flatness
// the v1 population measurement turned up.
//
//   1. The base is the tanh BLENDED WITH THE IDENTITY. A normalized tanh has
//      slope going to zero at both ends however you tune it — that saturation
//      was structural, not a bad parameter draw. Keeping (1-reach) of a
//      straight line underneath means the shape can be as S-like as it wants
//      and still never stop moving.
//
//   2. The ripple is scaled by its HEADROOM — the distance from the base curve
//      to whichever rail is nearer — so it approaches 0 and 1 asymptotically
//      instead of slamming into them. v1 clamped, and a clamp is a plateau;
//      13.9% of the average v1 curve sat pinned at a rail.
//
// The 0.5 factor keeps the ripple strictly inside the headroom, so the result
// lives in (0,1) with no clamping needed anywhere in the function.
inline float LiciumCurveEvalV2(const LiciumCurve& c, float x)
{
    if (x < 0.0f) x = 0.0f;
    if (x > 1.0f) x = 1.0f;

    float a = tanhf(c.k * (0.0f - c.pivot));
    float b = tanhf(c.k * (1.0f - c.pivot));
    float d = b - a;
    float s = (d > 1e-6f || d < -1e-6f) ? (tanhf(c.k * (x - c.pivot)) - a) / d : x;

    float y0 = (1.0f - c.reach) * x + c.reach * s;

    if (c.tension != 0.0f)
    {
        float env  = 4.0f * x * (1.0f - x);
        float head = (y0 < 1.0f - y0) ? y0 : (1.0f - y0);
        y0 += c.tension * 0.5f * env * head * sinf(2.0f * LICIUM_PI * (float)c.lobes * x);
    }

    // Belt and braces. The math above cannot leave [0,1], but the table this
    // feeds is quantized and sampled by a shader, and a stray NaN from an
    // extreme draw would poison a whole preset rather than one pixel.
    if (y0 < 0.0f) y0 = 0.0f;
    if (y0 > 1.0f) y0 = 1.0f;
    return y0;
}

inline float LiciumCurveEvalFamily(const LiciumCurve& c, float x)
{
    return (c.family >= LICIUM_CURVE_FAMILY_V2) ? LiciumCurveEvalV2(c, x)
                                                : LiciumCurveEval(c, x);
}

// Bake to a table of LICIUM_CURVE_LUT_W floats in [0,1]. The engine quantizes
// these to 8 bits on upload; the prospector uses them as-is.
inline void LiciumCurveBake(const LiciumCurve& c, float* out)
{
    const float inv = 1.0f / (float)(LICIUM_CURVE_LUT_W - 1);
    for (int i = 0; i < LICIUM_CURVE_LUT_W; i++)
        out[i] = LiciumCurveEval(c, (float)i * inv);
}

inline void LiciumCurveBakeSeed(unsigned int seed, float* out)
{
    LiciumCurve c = LiciumCurveFromSeed(seed);
    LiciumCurveBake(c, out);
}

// v2 bake. The blend and the headroom scaling remove the two KNOWN sources of
// flatness, but they do not by themselves PROVE the result never flattens: a
// steep ripple can still run opposite the base slope hard enough to cancel it
// somewhere in the middle. So the guarantee is enforced rather than argued —
// bake, measure the shallowest step, and if it is under the floor, ease the
// ripple off and bake again.
//
// This terminates: at tension 0 the curve is (1-reach)*x + reach*monotone,
// whose every step is at least (1-reach) of a straight line's, and reach is
// capped at 0.85 while the floor is 0.15. Deterministic, so a seed still
// always gives the same table.
inline void LiciumCurveBakeV2(const LiciumCurve& in, float* out)
{
    const float lin   = 1.0f / (float)(LICIUM_CURVE_LUT_W - 1);
    const float floorStep = lin * LICIUM_CURVE_MIN_SLOPE;
    const float inv   = 1.0f / (float)(LICIUM_CURVE_LUT_W - 1);

    LiciumCurve c = in;

    for (int iter = 0; iter < 16; iter++)
    {
        for (int i = 0; i < LICIUM_CURVE_LUT_W; i++)
            out[i] = LiciumCurveEvalV2(c, (float)i * inv);

        float worst = 1.0f;
        for (int i = 1; i < LICIUM_CURVE_LUT_W; i++)
        {
            float d = out[i] - out[i-1];
            if (d < 0.0f) d = -d;
            if (d < worst) worst = d;
        }
        if (worst >= floorStep || c.tension == 0.0f)
            return;

        c.tension *= 0.7f;
        if (c.tension < 0.02f && c.tension > -0.02f)
            c.tension = 0.0f;   // next pass is the monotone base, which always passes
    }
}

inline void LiciumCurveBakeSeedFamily(unsigned int seed, int family, float* out)
{
    if (family >= LICIUM_CURVE_FAMILY_V2)
    {
        LiciumCurve c = LiciumCurveFromSeedV2(seed);
        LiciumCurveBakeV2(c, out);
    }
    else
    {
        LiciumCurve c = LiciumCurveFromSeed(seed);
        LiciumCurveBake(c, out);
    }
}

#endif // LICIUM_CURVES_H
