# Bandit L2 design — layers, the spiral family, and remix (DRAFT)

*Status: **DRAFT — awaiting Gary's blessing.** Nothing here is implemented;
no code until the Decisions section is resolved. Parent doc:
`BANDIT_DESIGN.md` (L1, blessed 2026-07-16 — its settled decisions are not
reopened here). Inputs: Gary's spiral square-lock note (2026-07-17, shelved
from roll L1-BB43), his remix idea (2026-07-17 session end, parked in
SESSION_STATE.md), and the layers/buffer-count decision (2026-07-17: layer
extension is DESIGN-FIRST — a doc must name the layers it wants).*

## What L2 is

Three upgrades to the one-armed bandit, one genver bump:

1. **Layered composition** — rolls stop being a single medium. An
   *environment* field, *actor* inks living in it, and an *atmosphere*
   light pass compose into one visual. This is where the richer light Gary
   expects (the "dark rolls are natural for v1" resolution) actually
   arrives.
2. **The spiral family** — the square-lock mechanism becomes a first-class,
   protected behavior, and the generator learns the three moves on Gary's
   wishlist: field scale (squares small or large), child spirals growing on
   a spiral's spine, and axis-mirrored spirals.
3. **Remix** — any Licium roll on disk can be a *donor*: keep part of what
   it is (its palette, its medium, its ink) and re-roll the rest. "How does
   this roll behave with other shaders, buffers, layers, colors" becomes a
   button instead of a wondering.

## Clean-room rule (unchanged, hard constraint)

The old tree stays closed. The spec is this doc + `BANDIT_DESIGN.md` +
remembered lessons. All engine-side code stays `MILKDROP_STANDALONE`-gated;
the Winamp DLL builds pristine. The L1 lessons carry forward verbatim:
equilibrium law (`gain/(1-decay)`, hue-preserving clamps), fixed-spec LCG
determinism, the audio guarantee (verified live, never from stills), palette
discipline (one luminance-sorted ramp), and the variety lesson (any
structural element constant across rolls becomes THE sameness the eye locks
onto — so every new structural element must be a rolled draw, not a
constant).

## Layers — within the existing four buffers (recommended)

The 2026-07-17 decision said: no buffer-count extension until a design doc
names the layers it wants. This doc names them — **and they fit in the four
slots the M2 engine already has.** No engine change, no include.fx change.

| Slot | Res | L2 role |
|------|-----|---------|
| buf0 | full | **Environment** — the slow medium: curl-advected field, long decay (the L1 recipe lives here, evolved) |
| buf1 | full | **Actors** — faster ink layer: 1–3 injectors with their own motion + shorter decay, advected by the SAME velocity field as buf0 (shared field = composition coherence; separate decay = separate character) |
| buf2 | 1/8 | **Atmosphere** — cheap blurred luminance of (buf0 + buf1), read by comp as a soft glow/light volume (this is the "richer light") |
| buf3 | — | unused in L2 (reserve; costs nothing) |

Comp composes: environment shaded through the deep→mid stops, actors
through the accent stop, atmosphere as additive sheen light. One palette
ramp still rules everything (palette discipline holds).

Knob additions (appended to the L1 draw order — L1 knobs keep their
positions; GENVER bumps to L2 anyway): actor count (1–3), actor decay,
actor inject gain (equilibrium-scaled), actor motion pattern (child-spiral
planting is one of these — see the spiral family), atmosphere
strength, layer-blend weights. Exact ranges settled at implementation
against live grabs, recorded in the doc as amendments (L1 tuning
convention).

## The spiral family

**Protected behavior (do not "fix"):** the LATTICE archetype's
counter-rotating cells flatten their shared boundaries into square domain
walls — long-lived spirals bleed into surrounding slightly-bulged squares
that read as glass cubes. That mechanism survives every L2 change; any
tuning that would smooth those walls away is a regression.

New rolled draws (Gary's wishlist, 2026-07-17):

- **Field scale** — the lattice/archetype cell size becomes a knob
  (squares small or large; also scales the other archetypes' feature
  size).
- **Child spirals (fractal law — refined by Gary, 2026-07-18)** — like a
  fractal: the child's pull starts at the trunk, 90 degrees off the parent,
  and smaller. Gary's physics doubt is correct AND resolvable: the visible
  arm is not a backbone — it is ink stretched by wind, nothing to hang a
  child on. But the TRUNK is analytic: the parent swirl's center, rate, and
  phase are rolled knobs, so the ideal streamline (the log-spiral locus the
  arm forms along) is computable without ever being drawn. Children are
  planted AT computed points along that locus — smaller by a rolled ratio
  (~0.25–0.4), each oriented 90° to the local arm tangent so its wind
  CROSSES the parent arm instead of following it (crisscross by
  construction).
  - **Layer law (the part that makes it work — Gary's call):** children do
    NOT join the parent's field. Same-field children fight the parent's
    wind and shred the arm into domain walls — square-lock IS that
    mechanism, and it stays where it belongs (lattice). Children live on
    **buf1, the actors layer**, as their own wind field seeded from the
    parent's analytic geometry (deterministic: same seed, same planting
    points), composited over buf0. Consequence: *child spirals is an actor
    motion pattern, not a medium change* — this item and the layered
    composition are one mechanism.
  - **Color law (Gary): each child generation is palette-advanced one
    stop.** The child layer samples the SAME ramp shifted +1 (child deep =
    parent's first interior stop, wrapping toward accent), so parent and
    child harmonize by construction and generations read at a glance. One
    ramp still rules everything (palette discipline holds).
  - **Depth:** v1 ships fractal depth 1 (parent + children). Grandchildren
    are the same planting law applied recursively inside buf1; field cost
    is linear in total center count, so any future depth-2 gates on a total
    center cap (~12), not on depth. Parked until depth-1 is seen live.
- **Symmetry twin (mirror OR 180° — refined by Gary, 2026-07-18)** — the
  field is optionally composed with a transformed copy of itself, and the
  transform is itself a rolled draw with two physically distinct variants:
  - *Reflection* across x, y, or a rolled diagonal:
    `f(uv) + f(mirror(uv))`. Reflection REVERSES winding, so the twin's
    wind opposes the parent's — domain walls at the mirror line by
    construction (the square-lock texture Gary likes).
  - *180° rotation, trunk to trunk*: `f(uv) + f(2c - uv)` about a rolled
    center c, placed so the twin's trunk faces the parent's across c.
    Rotation PRESERVES winding — a co-rotating yin-yang pair whose seam
    SHEARS instead of walling. Not a flip: same chirality, different seam
    behavior, reads differently. Both variants stay in the pool.

All three are draws, not constants (variety lesson), appended in fixed
documented order.

## Remix

**Donor:** any roll whose file sits in `Licium\presets\` — Bandit L1/L2
rolls and Latent LX1 rolls (and the odd adopted foreign file, see below).
**Slots:** what a remix can keep vs re-roll, coarse on purpose:

| Slot | Keep = | Re-roll = |
|------|--------|-----------|
| palette | donor's 3-stop ramp | fresh palette draw (dressing pool applies) |
| medium | donor's field/archetype math | fresh field draw |
| ink/actors | donor's injection pattern | fresh actor draw |
| light | donor's sheen/atmosphere | fresh atmosphere draw |

**UX:** menu *Bandit > Remix current preset…* plus hotkey `c` (free per
HOTKEYS.md — "combine"). Rolls a remix of whatever preset is currently on
screen: a random non-empty keep/re-roll mask + a fresh 16-bit variation
seed. Press again for a different cut of the same donor.

**IDs:** `L2R-<donorID>-XXXX` where XXXX packs the 12-bit variation seed +
4-bit mask. Fully decodable: donor ID regenerates the donor (or the donor
FILE is the anchor for foreign/old-lineage donors — see decision 3),
mask says which slots were kept, seed drives the re-rolled draws.
Remixes save to `Licium\presets\bandit\` like every roll; filename carries
the full ID. Same byte-identical regen acceptance test.

**Mechanics note (why this is tractable):** for Licium-lineage donors the
generator can regenerate the donor's knob/draw state from its ID and
re-emit with masked slots swapped — no parsing of .milk text needed. For
foreign donors (e.g. `roll_LS2-0142A23B.milk`) the donor's text can't be
regenerated; v1 of remix supports them only for the PALETTE slot (colors
are plain preset lines, safely rewritable) — the deeper slots need
decodable lineage. That keeps remix honest: no fragile shader-text
surgery.

## IDs and reproducibility

- Plain rolls: `L2-XXXX`, same fixed LCG spec, L1 draw order preserved with
  L2 draws appended (documented order = the contract).
- Remixes: `L2R-<donorID>-XXXX` as above.
- `B` rolls L2. L1 IDs remain regenerable (the L1 code path stays; genver
  dispatch on the ID prefix). Old saved files load regardless.
- Acceptance: same seed twice → byte-identical; same remix (donor, mask,
  seed) twice → byte-identical.

## Engine integration (all standalone-gated)

- All inside `licium_bandit.cpp/.h` — no new engine hooks needed: rolls use
  the existing save/hard-cut path, remix reads the current preset's ID from
  the overlay state (and the loaded file path for foreign palette donors).
- Menu: Bandit popup grows *Remix current preset (C)*; HOTKEYS.md + F1
  updated in the binding commit (`c` occupied; free letters then p z).
- Automation: existing WM_APP+0x51 rolls exact L2 seeds; new WM_APP+0x54
  (next free) = remix with wParam packing mask+seed, for headless verify.

## Verification plan

1. Both projects build; vis_milk2.dll pristine (gating proof).
2. Same-seed L2 roll twice → byte-identical; same remix twice →
   byte-identical; an L1 ID still regenerates byte-identical to its saved
   file (genver dispatch proof).
3. Headless 8-seed sweep: grabs show layered composition (actors visibly
   distinct from environment; atmosphere glow present) and the new draws
   varying (cell scale, mirror, child spirals) — no single-structure
   sameness (12-seed contact-sheet check like the L1 archetype fix).
4. Square-lock regression check: a LATTICE-archetype seed still develops
   square domain walls (grab after 60s settle).
5. Remix headless: donor rolled, remix posted with each single-slot mask →
   exactly the expected lines differ in the emitted file (the M4b
   pool-diff proof technique).
6. Audio guarantee verified live with music (stills are invalid — L1
   lesson).
7. HOTKEYS.md + F1 updated in the same commit as the `c` binding.

## Decisions for Gary (bless or amend)

1. **Layer budget** — RECOMMENDED: the four existing buffer slots as tabled
   above (environment/actors/atmosphere + one spare), NO buffer-count
   engine extension. Coherence first; buf3 stays free for an L3 idea; the
   2026-07-17 layers decision is satisfied without touching the engine.
   *Alternative:* extend to 6 buffers now (mechanical, but pays the
   design-coherence cost with no named use for the extra slots).
2. **Remix scope** — RECOMMENDED: remix ships IN L2 as specced (Licium-
   lineage donors full-slot, foreign donors palette-only, `c` hotkey).
   *Alternative:* defer remix to its own L3/LX2 pass and ship L2 as
   layers+spirals only — smaller milestone, but the remix idea was the
   liveliest thread of the 07-17 session and its mechanics are cheap on
   top of L2's slot structure.
3. **Foreign-donor policy** — RECOMMENDED: palette-slot only, no shader-
   text surgery (honest + safe). *Alternative:* full text-splice remix of
   foreign files (powerful, but brittle string surgery on arbitrary
   presets — the "Error parsing shader" class of bugs lives there).

*(If all three go as recommended, implementation order: layers → spiral
draws → remix, one commit each, verification items alongside.)*
