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

struct LiciumCurve
{
    float k;        // sharpness: 0.05 (linear) .. ~24 (near-step)
    float pivot;    // where the S is centered, 0.30 .. 0.70
    float tension;  // overshoot amount, -1 .. 1 (0 = monotone ease)
    int   lobes;    // ripple lobes across the span, 1 .. 3
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

    return c;
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

#endif // LICIUM_CURVES_H
