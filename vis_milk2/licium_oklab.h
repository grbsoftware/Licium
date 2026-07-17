// licium_oklab.h — sRGB <-> oklab <-> oklch conversions + gamut clip.
//
// Written from Bjorn Ottosson's published reference (bottosson.github.io/
// posts/oklab, public domain / CC0 numbers), for the Palettes pillar
// (docs\PALETTES_DESIGN.md). Why oklch and not HSL: equal oklch Lightness /
// Chroma is equal PERCEIVED brightness / saturation across hues, so harmony
// schemes come out balanced — HSL's L lies (pure blue at L=0.5 reads far
// darker than yellow), which caused the old dark-wall era.
//
// Header-only, standalone-gated by its includers (no engine dependencies).

#ifndef LICIUM_OKLAB_H
#define LICIUM_OKLAB_H

#include <math.h>

static float LicSrgbToLinear(float c)
{
    return (c <= 0.04045f) ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

static float LicLinearToSrgb(float c)
{
    if (c < 0.0f) c = 0.0f;
    if (c > 1.0f) c = 1.0f;
    return (c <= 0.0031308f) ? c * 12.92f : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}

// sRGB (0..1, gamma) -> oklab
static void LicSrgbToOklab(float r, float g, float b, float* L, float* a, float* bb)
{
    r = LicSrgbToLinear(r); g = LicSrgbToLinear(g); b = LicSrgbToLinear(b);

    float l = 0.4122214708f*r + 0.5363325363f*g + 0.0514459929f*b;
    float m = 0.2119034982f*r + 0.6806995451f*g + 0.1073969566f*b;
    float s = 0.0883024619f*r + 0.2817188376f*g + 0.6299787005f*b;

    float l_ = cbrtf(l), m_ = cbrtf(m), s_ = cbrtf(s);

    *L  = 0.2104542553f*l_ + 0.7936177850f*m_ - 0.0040720468f*s_;
    *a  = 1.9779984951f*l_ - 2.4285922050f*m_ + 0.4505937099f*s_;
    *bb = 0.0259040371f*l_ + 0.7827717662f*m_ - 0.8086757660f*s_;
}

// oklab -> sRGB (0..1, gamma); out-of-gamut components come back outside 0..1
// (callers clip via LicOklchToSrgbClipped, which reduces chroma instead of
// clamping channels — channel-clamping shifts hue).
static void LicOklabToSrgb(float L, float a, float b, float* r, float* g, float* bb)
{
    float l_ = L + 0.3963377774f*a + 0.2158037573f*b;
    float m_ = L - 0.1055613458f*a - 0.0638541728f*b;
    float s_ = L - 0.0894841775f*a - 1.2914855480f*b;

    float l = l_*l_*l_, m = m_*m_*m_, s = s_*s_*s_;

    float lr = +4.0767416621f*l - 3.3077115913f*m + 0.2309699292f*s;
    float lg = -1.2684380046f*l + 2.6097574011f*m - 0.3413193965f*s;
    float lb = -0.0041960863f*l - 0.7034186147f*m + 1.7076147010f*s;

    *r = lr; *g = lg; *bb = lb;   // LINEAR here; gamma applied by the clipper
}

// oklch (L, C, h-radians) -> sRGB, reducing C until in gamut (bisection).
static void LicOklchToSrgbClipped(float L, float C, float h, float* r, float* g, float* b)
{
    float lo = 0.0f, hi = C;
    float rr = 0, gg = 0, bl = 0;
    for (int i = 0; i < 12; i++)
    {
        float c = (i == 0) ? hi : (lo + hi) * 0.5f;
        LicOklabToSrgb(L, c * cosf(h), c * sinf(h), &rr, &gg, &bl);
        int inGamut = (rr >= -0.001f && rr <= 1.001f &&
                       gg >= -0.001f && gg <= 1.001f &&
                       bl >= -0.001f && bl <= 1.001f);
        if (i == 0)
        {
            if (inGamut) break;   // requested chroma already fits
            continue;             // start bisecting
        }
        if (inGamut) lo = c; else hi = c;
    }
    if (!(rr >= -0.001f && rr <= 1.001f && gg >= -0.001f && gg <= 1.001f && bl >= -0.001f && bl <= 1.001f))
        LicOklabToSrgb(L, lo * cosf(h), lo * sinf(h), &rr, &gg, &bl);
    *r = LicLinearToSrgb(rr); *g = LicLinearToSrgb(gg); *b = LicLinearToSrgb(bl);
}

// Perceived luminance of an sRGB color (for the deep->accent sort). Rec.601
// weights on gamma values — matches the engine's own lum() convention.
static float LicSrgbLum(const float* rgb)
{
    return 0.32f*rgb[0] + 0.49f*rgb[1] + 0.29f*rgb[2];
}

#endif // LICIUM_OKLAB_H
