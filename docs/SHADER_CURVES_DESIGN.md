# Shader curves + LUT facility + families

*Status: **Steps 1-3 SHIPPED** (LUT facility 22511d5; vocab + floor policy
762c95e, genver LX5). **Step 4 (Families) DESIGN RESET 2026-07-25** to a separate
`LXF` trunk (+1 graft, NO genver bump) — see the Families section below; the old
compound-token/dial design is dead. Step 5 (generalize the LUT) deferred.
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

## Step 4 — Families as a SEPARATE TRUNK (the +1 graft)

**Direction reset 2026-07-25 (Gary + Opus 5, built by 4.8).** The earlier
"compound token in the cold alphabet + genver bump + capped guest share +
explore/exploit dial" design is DEAD. Gary's correction: *we keep cold-rolling
the same swarm/cold volume, and a family is a NEW TREE we grow off the forks each
time — brute-forcing every +1 possibility. One tree of even 100 branches is
almost nothing on top of 200-300k cold results.* So a family does NOT live inside
cold rolls; it rides ALONGSIDE the cold scan as its own lineage.

**Consequence: no genver bump.** The cold grammar is untouched, so LX5 seeds
decode identically, existing screens stay calibrated, and the LX5 haul stays
valid. "Capped share" and the "explore/exploit dial" dissolve — families never
compete for slots inside cold rolls; the only knob is how many family rolls you
emit next to the cold haul.

### The +1 graft operator

Given a liked (winning) tree, at EVERY node site — spanning both roots (ink +
psi) — graft exactly one node, keeping the existing subtree `T`:

- **Unary wrap:** `T -> u(T)`, `u in {abs, frac, floor, neg, curve@slot0,
  curve@slot1, curve@slot2}` = **7**.
- **Binary wrap:** `T -> b(T, term)` or `b(term, T)`, `b in` the 7 binaries,
  `term in` the 8 terminals, both orderings minus commutative dupes (add/mul/
  min/max are order-free; sub/div/lt are not) ~= **90**.

~100 grafts per site. A 40-node winner -> ~4,000 children = **~1.3% on top of a
300k cold scan** (Gary's "almost nothing" is exactly right). Leaf expansion needs
no separate operator — a binary wrap on a leaf *is* leaf expansion. `NK_CONST`
on the terminal side draws from a small fixed const ladder (kept in the frozen
enumeration contract).

### Identity = the enumeration index (no RNG for the family pass)

~4,000 children per family is small enough to enumerate EXHAUSTIVELY. No
sampling, no seed. The enumeration index IS the identity:
`id = (family_idx << 16) | graft_idx`, lineage **`LXF<cold_genver>-XXXXXXXX`**
(`LXF5-` while cold is LX5, so family rolls surface WITH the fresh gen-5 group in
X, not at the bottom). Fully decodable, byte-reproducible. Two disciplines buy
this:

- **`latent_families.txt` is append-only, never reordered** — a family's index is
  permanent (same law as the later-wins sidecars). It is part of the reproduction
  contract, like the swarm-table manifest.
- **The enumeration order is a frozen CONTRACT** (like the `NodeKind` enum block).
  Changing the operator set later bumps a *family* version, not the cold genver.

### What a frozen family record stores

To reproduce genver-independently, each record freezes: origin roll id + origin
seed + decay + the 3 curve seeds + the serialized node arrays for both roots.
Curves/palette for a family child re-derive from the stored origin seed (so the
child keeps the winner's curves); a `curve@slot` graft reuses one of the three.
The frozen node array means a later cold-genver bump can NEVER silently rebuild a
promoted family into a different tree.

### Which children to emit (the non-obvious call)

Family children start from an already-passing tree, so their screen pass-rate is
FAR above cold 0.9% — expect maybe half of ~4,000 to survive, ~2,000 passers for
~40 emit slots. **Stratify by graft SITE, round-robin** — you want "what happens
if I add something *here*" across all the different heres, not 40 variants at one
fork. Maximum information per grading session, still deterministic.

`LX_MAX_SIZE 48` / `LX_MAX_DEPTH 6` will bite: promote a 46-node winner and most
grafts screen out. Family search works best on small-to-mid winners — expected,
not a bug.

### Pipeline (four small pieces)

1. **`f` hotkey in X** appends the roll id to a pending-promotion sidecar
   (`latent_promote.txt`, near-copy of the `e` error hotkey). `f` again un-queues.
2. **`--promote`** rebuilds each pending tree from its seed under the CURRENT
   grammar, serializes it, appends to `latent_families.txt`. **TRAP:** must run
   while the roll's genver still matches `LX_GENVER` — the prospector can only
   rebuild a tree from a seed under the grammar that made it. Materialize + freeze
   at promotion time; the frozen text is genver-independent forever after.
   Promote lazily across a genver bump and the ids silently rebuild differently.
3. **Family pass** rides the normal scan (one command, cold budget unchanged,
   its OWN emit cap so families don't crowd the cold haul).
4. Triage + X as normal. Liked children get promoted again -> round 2.

**TRAP (parser):** `LatParseId` reads `LX`, then `S`, then digits. `F` is
neither, so `LXF5-...` parses gen = -1 and every family roll sinks to the bottom
of X (the exact bug class that buried the LX5 haul under LXS4). Teach the parser
`F` as a trunk marker in the same commit.

### The clean framing

Your votes are the selection function, `+1` is the mutation, rounds are depth: a
human-in-the-loop beam search, beam width = how many you promote. No
combinatorial explosion because YOU are the filter. Honest caveat: this DEEPENS
around winners, it does not WIDEN — the "same behaviors everywhere" ceiling is
scaffold-level (fixed swirl geometry) and neither cold nor family search touches
it (that's rung 2/3). Expect better versions of things you already liked; do not
read a flat result as the family mechanism failing.

## Step 5 — generalize the LUT (deferred)

Deferred to a later run (Gary, 2026-07-25): the second-client refactor
(palette-ramps / the audio layer ride the same bake/cache/bind facility). Build
Families first; step 5 is architecture with less immediate payoff. A palette IS a
color ramp IS a 1-D LUT; the audio-texture layer (`AUDIO_TEXTURE_DESIGN.md`) is a
data texture — same primitive (bake data, cache, bind, read). Do NOT over-abstract
before that second client is real.

## Build order (Families)

1. **Design doc** (this rewrite).
2. **Engine/X:** `f` promote hotkey + `LatParseId` `F`-trunk + `RollCompare`
   trunk generalization + legend/glyph. Rebuild.
3. **Prospector `--promote`:** rebuild+serialize+freeze liked trees to
   `latent_families.txt` (genver guard).
4. **Prospector family pass:** enumerate +1 grafts, screen, stratify-by-site
   emit as `LXF<gen>-` rolls (own emit cap).

White-room-safe throughout: GP grafting / ADFs, exhaustive enumeration,
parametric easings, spectral analysis are all standard public techniques.
