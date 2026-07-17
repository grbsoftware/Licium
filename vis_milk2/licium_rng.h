// licium_rng.h — the Licium tools' shared deterministic RNG.
//
// FIXED SPEC — part of every tool's ID contract (Bandit L-lineage, Palettes
// P-lineage). Do not "improve" without bumping the GENVER of every tool that
// draws from it. A private LCG, never rand(): rand() is seeded per-process
// and its sequence is a platform/CRT detail, so an ID rolled today would not
// regenerate on another box or another compiler. Draws take the HIGH 16 bits
// because an LCG's low bits have short periods.
//
// Each tool seeds its OWN instance; streams never mix.

#ifndef LICIUM_RNG_H
#define LICIUM_RNG_H

struct LiciumRng
{
    unsigned int state;

    void Seed(unsigned short seed)
    {
        state = 0x9E3779B9u ^ ((unsigned int)seed << 16) ^ (unsigned int)seed;
    }
    unsigned int Next16()
    {
        state = state * 1664525u + 1013904223u;
        return (state >> 16) & 0xFFFFu;
    }
    // Uniform float in [lo,hi].
    float F(float lo, float hi)
    {
        return lo + (hi - lo) * (Next16() / 65535.0f);
    }
    // Uniform integer in [lo,hi].
    int I(int lo, int hi)
    {
        return lo + (int)(Next16() % (unsigned int)(hi - lo + 1));
    }
};

#endif // LICIUM_RNG_H
