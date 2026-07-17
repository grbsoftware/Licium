# Palettes design — Licium clean-room re-port (v1)

*Status: DRAFT — awaiting Gary's blessing. Written 2026-07-16, after Bandit v1
landed (M3). Recommended decisions are marked; nothing here is code yet.*

## What Palettes is

Press **V**, get a wall of color palettes. Like the ones that speak to you,
pass the rest. Likes accumulate into a pool, and Bandit starts dressing its
rolls in palettes you actually chose — the first place Licium's output bends
toward its user's taste. It is the second of the three tool pillars
(Bandit / **Palettes** / Latent), and the training ground for the taste model
that comes later.

## Clean-room rule (hard constraint)

The old palette system (voting wall, multi-strategy roller, oklab header,
curated library header, taste model) lives in the closed 3.33-contaminated
tree and reached a mature state there. **No file in that tree may be opened,
diffed, or copied.** The spec is this document + remembered behavior. Two
things make this easy here:

- The **curated palette library** is regenerated from its original public
  sources, not carried: `Experience-Monks/nice-color-palettes` (500, MIT) and
  `mattdesl/dictionary-of-colour-combinations` (348 Sanzo Wada groupings,
  MIT-licensed reconstruction; group colors.json entries on shared
  `combinations` ids — note US spelling "colors"). A fresh generator script
  fetches and bakes them into a new header. Same data, zero old code.
- The **oklab conversions** are written from Ottosson's published matrices
  (bottosson.github.io/posts/oklab, public domain), as before.

Carried-over *knowledge* (the laws that were paid for in bad walls):

- **HSL lies about brightness.** Equal HSL lightness is not equal perceived
  brightness (pure blue at L=0.5 reads far darker than yellow) — the root of
  the old "dark wall" era. OKLCH is the workhorse; HSL survives only as one
  minor strategy.
- **Votes bind to colors, not seeds.** A roller change remaps seed→palette
  and orphans every vote (this happened; 88 votes lost). Votes are stored as
  quantized color lists (`#RRGGBB,#RRGGBB,... 1|0`, later-wins, hash-matched),
  so they survive any generator change and apply to imported palettes too.
- **Luminance-sorted ramps, deep → accent; no pure white anywhere.**
- **Black separators on the wall.** Swatches judged against white wash out
  and liked palettes look pastel.
- **No heavy recompute per vote** (the old wall lagged when a header line
  retrained a model on every keypress). Votes append; anything expensive
  happens at wall-close.

## V1 scope — the proposed slice

1. **Multi-strategy roller.** A palette is deterministic from a 16-bit seed
   (ID `P1-XXXX`, same LCG spec as Bandit, its own stream). A seed-hash
   (independent of the knob stream) dispatches the strategy:
   - **OKLCH harmony** (~32%) — harmony schemes with equal perceived
     chroma/lightness across hues; gamut-clip by chroma reduction.
   - **Curated** (~30%) — pick from the 848-entry regenerated library.
   - **Cosine/IQ** (~23%) — `a + b·cos(2π(c·t+d))` smooth ramps; constrain
     `a+b ≤ 1`, `a−b ≥ 0`; the future basis for correlated color cycling.
   - **HSL harmony** (~15%) — the legacy Radiance-style roller, kept for
     variety, luminance ladder climbing to bright accents (~0.86).
   Every strategy outputs the same shape: 3–5 colors, luminance-sorted.
2. **The voting wall.** `V` (or menu: Palettes > Voting wall) opens a modal
   grid — 6×5 = 30 seeded ramps per page, black separators. Keys: arrows
   move, `Y`/`Space` like, `N`/`X` pass, `U` clear vote, `PgUp/PgDn` page,
   `V`/`Esc` close. The wall eats all other keys while open, and the engine's
   preset auto-advance is held (the old bug: the timer kept running behind
   the modal and yanked the preset mid-vote).
3. **Votes file:** `Licium\palette_votes.txt`, color-bound format above.
   Global scope for now (expeditions don't exist in Licium yet; the file
   format doesn't care where it lives when they do).
4. **Bandit integration (the payoff):** when the liked pool is non-empty,
   ~70% of Bandit rolls are dressed in a liked palette (side-hashed pick, so
   the knob stream is untouched and structure stays seed-pure); 30% keep
   their rolled palette as the exploration floor. The saved `.milk` freezes
   whatever colors the roll wore — the artifact is stable forever. The doc
   below records the regen caveat this implies.

Not in v1 (growth path): the taste model (Phase 1 LR per
`LATENT_SPACE_ML_DESIGN.md` — it needs votes to exist first), duels/Elo,
color cycling, expedition scoping, image-extraction strategies (declined).

## IDs and reproducibility

- Palette ID `P1-XXXX` regenerates its palette exactly (same LCG spec as
  Bandit's, seeded/stepped identically, own GENVER `P1`).
- **Bandit regen caveat (decision 1 below):** a *dressed* roll's `.milk` file
  is byte-stable, but re-rolling its `L1-XXXX` ID later regenerates the
  structure exactly while the colors follow the *current* pool. This is the
  old system's accepted trade ("structure reproducible, colors follow the
  pool"). The alternative — embedding palette colors in the roll ID — would
  make IDs long and mutable-pool-dependent anyway.

## Engine integration (all standalone-gated)

- **New files:** `vis_milk2\licium_palettes.cpp/.h` (roller strategies, oklab
  header inline or `licium_oklab.h`, wall state, votes file), plus the
  regenerated `licium_palette_lib.h` (auto-generated, committed) and its
  generator script under `tools\`.
- **Key hook:** `V` binds through the existing `HostHandleChar` (it was built
  generic for exactly this). While the wall is open the host consumes every
  WM_CHAR, and a new `HostHandleKeydown` hook consumes arrows/PgUp/PgDn —
  remembering the WM_CHAR/WM_KEYDOWN duality: each action lives in exactly
  one message type, and Space's keydown is swallowed so the engine's
  Space=next-preset never fires under the wall.
- **Render:** the wall draws colored quads through the engine device plus
  labels through `m_text` (raw DrawText renders nothing — the Bandit overlay
  reconfirmed it), from a gated hook in the UI render pass.
- **Auto-advance hold:** one gated condition in the preset-advance path:
  while a host modal is open, the soft-cut timer and hard-cut paths hold.
- **Menu:** the greyed **Palettes** heading becomes: *Voting wall (V)* ·
  *Open votes file* · *(later: taste-model status)*.
- **Automation hook:** `WM_APP+0x52`: wParam=1 opens the wall, 0 closes it;
  votes are driveable by posting the wall's own keydown/char keys (post
  keydown-only for physical-key mimicry, per the old lesson). Headless
  verify: open wall → grab → post votes → close → inspect votes file.
- **HOTKEYS.md + F1** updated in the binding commit (`v` is free — only used
  inside text prompts).

## Verification plan

1. Both projects build; vis_milk2.dll stays pristine (gating proof).
2. Same palette seed twice → identical color lists (determinism).
3. Wall opens headlessly, Spout grab shows the 6×5 grid with distinct
   palettes; strategy mix visibly varied (harmonies + gradients + curated).
4. Posted votes append correct color-bound lines; re-vote overwrites
   (later-wins); U clears.
5. Auto-advance held while wall open (baseline advances, wall-open freezes,
   resumes on close).
6. With ≥1 like: Bandit rolls dress from the pool ~70/30 over a fixed seed
   set; dressed roll's `.milk` carries the pool palette; the same roll seed
   with a different pool regenerates same structure, different colors
   (documents the caveat).
7. Library regen script re-run → identical header (source-locked).

## Growth path (post-v1, separate blessings)

- **Taste model** — Phase 1 logistic regression per `LATENT_SPACE_ML_DESIGN.md`
  (16 features, ≥30-verdict + AUC gates, softmax dressing, exploration floor).
  The wall's header line then shows taste weights; candidates span strategies.
- **Color cycling** — animate the cosine strategy's phase (correlated
  channels, never independent RGB).
- **Duels/Elo** (whole-roll rating) and **expeditions** — their own docs.

## Decisions for Gary

1. **Dressed-roll regen trade** — RECOMMENDED: accept the old trade
   (artifact file byte-stable; ID-regen keeps structure, colors follow the
   live pool). Embedding colors into roll IDs is the only alternative and it
   poisons ID brevity for every roll, dressed or not.
2. **Curated library in v1** — RECOMMENDED: yes, regenerated fresh from the
   two public repos (MIT/MIT). It was 30% of the old dispatch and the
   best-loved strategy; the regen script keeps the clean-room boundary and
   makes the library reproducible.
3. **Wall geometry** — RECOMMENDED: keep 6×5×pages with the gen-20 key map.
   It was verified pleasant in real use; no reason to redesign a working
   surface.
