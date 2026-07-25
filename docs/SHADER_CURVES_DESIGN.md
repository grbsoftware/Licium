# Shader curves + LUT facility + families

*Status: **Steps 1-3 SHIPPED** (LUT facility 22511d5; vocab + floor policy
762c95e, genver LX5). Steps 4 (families + explore/exploit dial) and 5 (generalize
the LUT to palette-ramps / the audio layer) remain — still design-only below.
Worked out live with Gary 2026-07-24, built 2026-07-25. Cross-refs:
[[milkdrop-latent-search]], `AUDIO_TEXTURE_DESIGN.md` (shares the LUT facility),
`LATENT_GROWTH_DESIGN.md` (promotion), `SHADER_BEHAVIOR_RESEARCH.md` (vocabulary
roadmap).*

*Build notes (2026-07-25): NK_CURVE is a unary kind sampling a seed-baked LUT;
curve seeds are side-hashes of the roll seed (CurveSeedOfSlot, 'CUR0'+slot,
31-bit) so trees stay seed-pure. Slots 0..2 tree-drawable, slot 3 = comp ramp
(Curve3 + smoothstep replace the fixed saturate cliffs; the additive sheen was
deliberately left alone pending a separate blowout diagnosis). Floor policy:
LX_HARD_QUOTA=1 hard step per build (excess remaps to soft curves, no extra
draw); screens reject audio-inside-a-hard-step and a hard-step-with-no-curve.
Verified: --selftest byte-identical, 3 rolls render clean in-engine (soft
gradients, no strobe), recorded scan [32.66M,33.66M) 0.895% pass, 40 emitted.
Open finding: curves don't fix the flat "bloom or go dark" ceiling — that's the
growth-ladder / families work, not this milestone.*

## Why (the diagnosis that started it)

Reading a real cursed roll (LXS4-01CDC968) end-to-end nailed the "horrific
flash" Gary sees:

- The prospector's vocabulary has **only hard nonlinearities** — `NK_FLOOR`,
  `NK_FRAC`, `NK_ABS`, `NK_NEG` (latent_prospect.cpp ~L65). There is **no
  `smoothstep`, `tanh`, `pow`-as-curve, sigmoid** anywhere. `pow` appears once,
  hardcoded in the fixed sheen line. So the generator *cannot* make a soft ramp
  — every roll cliffs because cliffs are the only tool.
- The flash is `floor()` with **audio inside its argument**:
  `buf0_7: f = floor(... + (fb - bass_att))`. The beat pushes the value across
  an integer boundary and `floor` snaps the WHOLE field 0->1 in one frame = the
  blinding full-field strobe. Then `f` (0 or 1) hard-injects the buffer, `dens`
  goes near-binary, and the comp ramp only ever shows its two endpoints.
- The black/white aren't Gary's palette: black = decayed / `f=0` on the near-
  black `deep` stop; white = the `sheen` specular term (`ret = ramp + accent *
  sheen * dens*0.5`) blowing highlights past the accent.
- Second cliff source: the comp **color ramp is fixed scaffold** — `dens =
  saturate(dot*3.0)` then three hard `saturate(dens*3 - k)` steps, baked into
  every emit regardless of the tree.

Lesson: color knobs can *reveal* this but not fix it. The fix is at the
generator — give it curves, and stop audio from tripping hard steps.

## The primitive: seed-addressable shaping curves, baked to LUTs

A **shaping curve** is a parameterized shape (not a fixed named function). A
couple of rolled knobs — **sharpness** and **tension/overshoot** — span a
continuous space where `floor` (sharp), `smoothstep` (soft), and a ball-bounce
(overshoot; the "cusp then spring past rest" Gary described) are just three
coordinates. Overshoot is a hard requirement: monotonic eases (smoothstep,
gamma) can't bounce, so the family needs a tension axis, not just sharpness.

**Chosen implementation: LUT, not inline math** (Gary's call, 2026-07-24). A
curve is seed -> params -> a fixed shape -> a **256-value lookup table**. Rationale:

- "Expensive" was never CPU — it's GPU *per-pixel per-frame* (~15M evals/s). A
  branchy spline stalls GPU warps. A LUT is ONE cheap texture fetch regardless
  of how wild the curve is. So the LUT buys full spline freedom at flat runtime
  cost. (Gary re-derived color-grading LUTs from scratch — this is standard.)
- 256 entries loses nothing at the 8-bit color output; intermediate float
  buffers keep their precision (they don't read the color LUT).
- **Seed-addressable + cache-once:** a curve is fully determined by its seed, so
  the LIBRARY stores seeds (tiny) and an in-memory CACHE stores the baked tables
  keyed by id. First reference bakes once; every other use binds the cached
  table. Regen-from-seed is <1ms, so recompute-once-per-launch is fine (persist
  to disk only if we ever want zero recompute ever — a rounding error).

## Engine facility: a generic bound-data / sampler bank

Precedent already in the tree: the multipass buffer engine added generic
`sampler_buf0..3` ([[milkdrop-multipass-buffers]]). Curve LUTs are the same kind
of citizen — extra sampler slots (`sampler_curve0..N`) a shader reads with one
`tex` fetch. Pieces:

- **LUT manager:** bake table from seed, cache by id, bind to a sampler slot.
- Generated shaders **sample `sampler_curveN`** instead of computing inline.
- The preset **carries the curve-seeds it uses** (rides along like `texsize_bufN`
  does today); the engine bakes/binds on load.
- The prospector **rolls curve-seeds** and references slots.

**Generalize on the SECOND client, not the first.** A palette IS a color ramp IS
a 1-D LUT; the invisible audio-texture layer (`AUDIO_TEXTURE_DESIGN.md`) is a
data texture the shader samples. All the same primitive: bake data, cache, bind,
read. So the facility is really a generic sampler-bank that curves, palette-
ramps, and the audio layer all ride on. Build it concretely for CURVES first
(prove the pipeline with one client), design the bind interface so ramps/audio
slot in later. Do NOT over-abstract before the first client works.

**Curves apply to many things:** node outputs (the floor case), the color ramp
(= the cockpit's gradient-steepness knob, but rolled), movement, and the
audio->effect mapping. One curve vocabulary, used everywhere a signal is shaped.

## Cold-forever + promotion (families)

Gary re-confirmed **cold prospector forever** — no directed search, no
pheromone. Promotion works WITHOUT warming the search:

- **Curves are silent.** No per-curve vote. A liked *shader* credits the curves
  (and combinations) inside it — matches the standing law "votes dictate what
  flows to Bandit; mine -> name -> promote into ingredient tables."
- **A family = a promoted winning COMBINATION = a compound/macro token.** The
  generator's vocabulary is atomic today (one primitive per token). A family is
  a whole winning subtree promoted intact, drawn as a single token that expands
  to a **longer string** -> rolls that draw it start **more developed**, from an
  earned chunk instead of from atoms. (Genetic programming calls these
  "automatically defined functions.") The search stays EXACTLY as cold: you
  enrich the ALPHABET, you do not steer the walk. A seed still reproduces a roll
  byte-for-byte; the family expands deterministically.
- **Extraction — decide for v1:** promote the liked shader's *whole tree* (or its
  couple of characteristic subtrees) as a family — pragmatic given ~40
  votes/session is too thin for statistics. Later: *mine recurring subtrees*
  across many likes (the growth doc's `--mine` tooling) once there's signal.
- **Gating (swirl clause):** families + library curves are capped-share GUESTS —
  they enrich, never dominate. Even at full exploit, cap the earned share or a
  few beloved curves eat all diversity.
- **Explore<->exploit dial:** one scalar = P(draw from earned library/families vs
  fresh random seed). Fits the Bandit name. Its DEFAULT drifts explore->exploit
  as the library matures, so the public build ships "doped with an earned
  starter vocabulary" (the long game Gary already stated).

## Build order for the next session (proposed)

1. **LUT facility first** — bake-from-seed + cache-by-id + bind to one
   `sampler_curve0`, with a single hardcoded test curve. Prove a shader can read
   it live. (Cloning the buffer-sampler plumbing.)
2. **Shaping-curve vocab** — the parametric family (sharpness + tension) ->
   256-LUT; a `NK_CURVE` slot the emitter references; the prospector rolls curve-
   seeds. Also roll the **comp ramp softness** (kills the fixed-scaffold cliffs).
3. **The floor policy** — soft curves in the vocab; `floor`/`frac` rationed +
   never alone (co-occurrence screen); **ban audio inside a hard step** (spatial-
   arg floors = stripes OK; audio-arg floors = strobe, reject). One genver bump;
   screens recalibrate on first scan.
4. **Families** — extract from liked shaders -> compound macro tokens in the
   cold vocab; capped guest share; the explore/exploit dial.
5. Generalize the LUT facility to palette-ramps / the audio layer (second
   client).

## Open decisions to confirm before coding

- Curve-slot budget per preset (the `buf0..3` question, for curves) — a handful?
- Family extraction: whole-liked-tree (v1) vs subtree-mining (later) — leaning v1.
- LUT cache: in-memory recompute-per-launch (default) vs persist-to-disk.

White-room-safe throughout: LUTs, GP macros, parametric easings, spectral
analysis are all standard public techniques.
