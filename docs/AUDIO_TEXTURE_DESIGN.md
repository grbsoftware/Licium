# Audio texture design — the invisible fourth layer (DRAFT)

*Status: **DRAFT — NOT blessed, NOT scheduled.** Spitballed with Gary
2026-07-22; he explicitly does not expect to build this week. This doc exists
so the idea (and the reasoning behind each choice) survives the conversation.
Nothing here is settled; treat every "decision" below as a proposal.*

## The one-line idea

Add a **hidden buffer that holds the music as a small 2-D data texture** — a
rolling spectrogram — that the three *visible* layers sample from. The viewer
never sees it; it is a modulation source, not a picture. This is the
foundation that lets geometry / smear / color each respond to a *different*
truth about the sound instead of all flashing to the same brightness.

This is prior art in a new paradigm: Ableton's spectrum view, EQ-8 analyzer,
Serum's spectral display, iZotope, and — most directly — ShaderToy's audio
`iChannel` (row 0 = live FFT, row 1 = waveform). We crib the representation
and repurpose it as an *invisible* input. The only novel twist is that nobody
looks at it, which removes two constraints a real picture has (see below).

## Why it beats scalar sources

Today the rolls react to a handful of scalars (`bass/mid/treb`, their `_att`
smoothed forms, volume, a beat/transient). A scalar says *how much, right now*.
The audio texture says **where and when**:

- **Where** (frequency axis, y): a layer can read only the bass rows, or only
  the treble rows, *per screen position*. This is Gary's "earmark where the
  sound is coming from," made continuous across the whole spectrum instead of
  three buckets.
- **When** (time axis, x): a layer can read *what the music did half a second
  ago*. This is how trails/echoes trace the beat across the screen instead of
  the whole field flashing in unison on the current frame. It directly attacks
  the standing complaint ("all music applied to brightness... they just flip
  colors" — see [[milkdrop-md2-port]] growth notes / SHADER_BEHAVIOR_RESEARCH).

## Two constraints removed because it is invisible

A displayed image is bound by both of these; a data texture is bound by
neither:

1. **256 levels.** A shown image is 8-bit — 256 steps per channel. Audio's
   dynamic range is huge (cymbal tail vs. kick); 256 steps band it visibly. A
   data texture is a **float texture** (R16F/R32F) — effectively continuous.
   This is Gary's "much finer gradients than 256 light levels." Free the moment
   we stop treating it as a picture.
2. **One number per cell.** A grayscale spectrogram stores one value
   (magnitude), so brightness carries everything. But a texel is **four float
   lanes (RGBA)**. Since no eye needs the color for legibility, we spend the
   channels on *independent data*, not on prettifying one value. (A "heatmap"
   in the DAW sense dresses ONE value in color for human eyes — still one
   dimension. We go the other way: four real dimensions, no eyes involved.)

## Proposed channel packing (per texel = per pitch, per moment)

| Lane | Carries | Notes |
|------|---------|-------|
| R | **loudness** at this frequency | the classic spectrogram magnitude |
| G | **rising vs falling** (spectral flux at this bin) | attack vs decay — lets a layer bloom on onsets, settle on tails |
| B | **stereo balance** (left ↔ right) at this frequency | real spatial position from the mix; a layer can lean L/R |
| A | **tone vs noise** (harmonicity / spectral flatness locally) | clean note ↔ hiss — the "missing tonal information" Gary flagged; magnitude alone can't tell a sung note from a snare *tss* |

A visible layer samples one texel and gets a tiny paragraph about the music at
that spot: *how loud, getting louder or quieter, where in the stereo field, how
tonal.* Open question: is 4 lanes the right budget, or start with R+G only and
grow? (Fewer lanes = simpler upload, smaller texture.)

## Dimensions

- **2-D** (freq × time) is the baseline and already a big jump.
- **"3-D feel" without a volume texture:** a layer reads the texture's value as
  a **height** and its slope (gradient) as a **direction** → the music becomes
  *terrain* the visuals flow over (ridges on bass hits, valleys in the quiet).
  This is a *reading* technique, not extra storage.
- **Genuine extra axis worth having: stereo** — already proposed as the B lane
  rather than a separate texture, but if L/R needs full spectral detail it
  could be two half-width regions instead of one averaged lane. Undecided.

Proposed size: something like 128–256 (freq bins) × 128–256 (time columns),
R16F or R32F, 4 channels. Exact size TBD against cost.

## How it fits Licium's architecture

The engine already has the four-buffer feedback system (`buf0..3`, the M2
multi-pass work — see [[milkdrop-multipass-buffers]]). Natural fit: the
audio texture is a **new sampler** (working name `sampler_audio`), fed
*differently* from the visual buffers — the host uploads one FFT-derived
column per frame (a tiny CPU→GPU write) instead of the visual pipeline drawing
into it. The visible layers' warp/comp shaders sample it.

Clean-room-safe: standard technique, public paradigm, same family as the
buffer engine we already ported. Engine-level and ungated (M2 precedent), OR
standalone-gated if we want to keep the Winamp DLL pristine — decide at build
time.

## Honest cost

This is an **engine layer, not a cockpit knob** — its own milestone:

- allocate a float MRT texture (multi-channel render target / data texture),
- per-frame FFT → derive the 4 lanes → upload one column, scroll,
- expose new sampler(s) to the shader compile path,
- recompile shaders to reference it; extend the roll grammar (and/or the Tune
  cockpit) to *use* it.

It is **foundation both the cockpit and the roll grammar draw on** — the growth
ladder (getting audio into geometry, rung 2/3) wants exactly this. If built, it
makes *every* roll capable of the spatial/temporal reactivity Gary is chasing;
the Tune cockpit knobs then become ways to *dial how hard each layer taps it*.

## Open questions (nothing decided)

1. Channel budget: 4 lanes now, or R+G first and grow?
2. Texture size / format (perf vs. detail).
3. Where the FFT comes from — reuse the engine's internal FFT, or run our own
   in the host for full-resolution bins? (bass/mid/treb are coarse; the texture
   wants finer.)
4. Stereo as one B lane vs. two spectral halves.
5. Engine-level (ungated, benefits future non-standalone) vs. standalone-gated.
6. How rolls *reference* it — new grammar terms (rung 2/3), and/or a
   fixed set of taps the Tune cockpit exposes.

Companion doc: `TUNE_COCKPIT_DESIGN.md` (the knobs that would dial this).
