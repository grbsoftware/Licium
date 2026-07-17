# Bandit design — Licium clean-room re-port (v1)

*Status: **BLESSED by Gary 2026-07-16** — all three recommended decisions
accepted (see the resolved Decisions section at the bottom). Implement FROM
this doc; do not re-derive or reopen the settled decisions.*

## What Bandit is

Press **B**, get a brand-new visual. The engine rolls a random preset from a
generator (not a file browser), loads it instantly, and stamps the roll with a
short ID. The ID *is* the seed: type the same ID later and you get the exact
same preset back, byte for byte. It's a one-armed bandit for shader space —
the first of Licium's three tool pillars (Bandit / Palettes / Latent).

## Clean-room rule (hard constraint)

The old Bandit reached generation 26 inside the private 3.33-contaminated
tree. **That tree stays closed — no file in it may be opened, diffed, or
copied while building this.** The spec is: this document + remembered behavior
(recorded in session memory and handoff notes). Everything is written fresh
against the pristine MD2 base, gated `MILKDROP_STANDALONE` like every other
Licium feature, so the Winamp DLL build stays pristine.

What we carry over is *knowledge*, not code — the hard-won behavior lessons:

- **The equilibrium law:** anything repeatedly added into a feedback surface
  converges to `gain/(1-decay)` (~25–125x). Every ink gain must scale with
  `(1-decay)`, and clamps must be hue-preserving vector scaling (per-channel
  `min()` makes white plates).
- **Determinism:** the roll uses its own LCG, never `rand()` or anything
  platform-flavored, so an ID regenerates identically forever.
- **Audio guarantee:** every roll must dance. Injection and at least one
  visible parameter are driven by bass/mid/treb — no deaf rolls.
- **Palette discipline:** colors come from one luminance-sorted ramp
  (deep → accent), never independent RGB rolls (gray mud). No pure white.

## V1 scope — the blessed slice

The user-facing behavior, end to end:

1. **B** (or menu: Bandit > Roll) rolls a fresh seed.
2. Generator turns the seed into a complete `.milk` preset: a buf0
   feedback-fluid medium (curl-advected injection + decay — the proven
   "luminous plumes" recipe, exercising the M2 buffer engine) + comp shader
   doing gradient-sheen shading through the palette ramp.
3. Preset is saved to `Licium\presets\bandit\bandit_L1-XXXX.milk` and loaded
   with a hard cut.
4. **ID overlay:** `L1-XXXX` appears in a corner of the window while the roll
   is current (cleared on the next preset change). The ID is also copied to
   the clipboard on every roll.
5. The saved file is the permanent artifact — rolls are never lost, and any
   roll can be reloaded from the normal preset list.

Not in v1 (growth path, below): multi-ink layers, behavior tags in filenames,
palette voting, expeditions, taste model, duels.

## IDs and reproducibility

- **Format:** `L1-XXXX` — `L<genver>` + 16-bit seed in hex. The `L` prefix
  marks the Licium lineage; old-tree IDs (`5-XXXX` … `26-XXXX`) and PS-script
  IDs (`1-XXXX`, `2-XXXX`) are a different lineage and never interchange.
- **LCG (fixed spec, part of the ID contract):** 32-bit state seeded as
  `state = 0x9E3779B9 ^ (seed << 16) ^ seed`; step
  `state = state * 1664525 + 1013904223`; draws take the high 16 bits.
  Every knob draws from this one stream in a fixed documented order.
- **GENVER bump rule:** any change to knob math, draw order, or the emitted
  template bumps the generator version (`L1` → `L2`). Old IDs keep meaning
  "regenerate with that genver"; the saved `.milk` files cover versions we
  choose not to keep regenerable.
- **Acceptance test:** rolling the same seed twice must produce byte-identical
  preset files.

## Generator v1 recipe

Seeded knobs (one LCG stream, fixed order):

| Knob | Range (approx) | What it does |
|------|----------------|--------------|
| decay | 0.90–0.97 | trail persistence in buf0 |
| curl | small | swirl strength of the advection field |
| advect | small | how hard dye is pushed along the field |
| inject | 0.5–1.5 × (1-decay) | injection gain (equilibrium-safe) |
| inject pos/motion | varied | where the source sits and how it moves (fixes the old "everything is a rising plume" limitation) |
| iters | 1–3 | buf0 intra-frame ping-pong count |
| palette | 3-stop ramp | hue scheme + luminance ladder deep→mid→accent |
| sheen | 1–4 | comp gradient-highlight power |
| field archetype | 4 types | *(added 2026-07-16, appended draw)* the velocity-field composition itself: swirl / cell lattice / meandering stream / drift-weave. One fixed field formula made every roll the same comma-spiral in a different color (12-seed sweep confirmed); this roll is where composition variety comes from. Still one buffer, still curl-advected — within the blessed v1 scope. |

Structure: `bufN_` pass(es) run the medium; warp is a near-passthrough; comp
reads buf0, shades by gradient, maps through the palette ramp. Emitted shaders
target the engine-agreed PSVERSION. Injection is bass-scaled (audio
guarantee). Equilibrium clamp baked into the template, not the rolled math.

## Engine integration (all standalone-gated)

- **New files:** `vis_milk2\licium_bandit.cpp/.h` — knob rolling, preset text
  emission, file save, ID/clipboard/overlay state. Compiled only into
  `milkdrop_standalone.vcxproj`.
- **Hotkey:** one gated hook early in `HandleRegularKey()` (plugin.cpp):
  `HostHandleChar(ch)` — host-first, returns true if consumed. Generic on
  purpose: Palettes/Latent reuse the same hook later. `b`/`B` is free per
  `docs\HOTKEYS.md`; bind it and update the registry + F1 text in the same
  commit. Shadowed inside waitstring/menu modes, as the registry warns —
  acceptable for v1.
- **Menu:** the greyed **Bandit** heading becomes a live popup:
  *Roll (B)* · *Copy last ID* · *Open rolls folder*. Handled in the existing
  host WM_COMMAND path (`standalone_host.cpp`), private id block next to the
  0xE1xx range.
- **Loading:** host drives `g_plugin` directly (all-public CPlugin precedent
  from M1c) — write the file, point the engine at it, hard cut.
- **Overlay:** queue the ID through the engine's text manager during the UI
  render pass (lesson from the old tree: raw `DrawText` outside that path
  renders nothing). Small text, corner placement, survives until preset
  change.
- **Automation hook:** a WM_APP message (`wParam = seed`, documented in the
  header) rolls an exact seed, so Claude can verify headlessly: launch
  detached → post seed → Spout-grab → compare regen bytes. Mirrors the old
  0x5241 IPC in function, fresh in implementation.

## Verification plan

1. Build both projects; confirm vis_milk2.dll still builds pristine (gating
   proof, per the FABLE REVIEW convention).
2. Same-seed double roll → byte-identical files.
3. Headless: post 6 seeds via the automation hook, Spout-grab each, confirm
   six distinct living visuals + ID overlay visible in grabs.
4. ~~Audio check: grabs during music vs silence differ (rolls dance).~~
   **INVALID AS WRITTEN — corrected 2026-07-16 during implementation.** The
   engine's `bass`/`mid`/`treb` are *relative* measures (immediate / long-running
   average, `plugin.cpp` ~L9790), and in silence it explicitly pins them to
   **1.0** (`if (fabsf(long_avg[i]) < 0.001f) imm_rel[i] = 1.0f;`). So they sit
   near 1.0 in silence AND in steady music, deviating only on transients — a
   silence-vs-music still comparison shows no difference by design, and two such
   grabs were confirmed near-identical. Consequences: (a) baseline equilibrium is
   `injectK` (bass_att≈1), which is what the equilibrium math assumes — correct
   by construction; (b) the audio guarantee shows up as *beat pulsing*, so it is
   verified live with music, not from stills.
5. HOTKEYS.md + F1 overlay updated in the binding commit.

## Growth path (post-v1, separate blessings)

- **L2 — variety:** multi-layer composition (environment/actors/atmosphere),
  ink families, behavior tags in filenames (`bandit_<tags>_L2-XXXX.milk`).
- **Palettes pillar:** multi-strategy palette roller + the voting wall (V) —
  its own design doc; Bandit then dresses rolls from liked palettes.
- **Expeditions:** named roll folders with manifests.
- **Latent pillar:** duels/taste-model/prospector per LATENT_SEARCH_DESIGN.md.

## Decisions — RESOLVED (Gary, 2026-07-16)

1. **V1 generator richness:** the single-buffer fluid recipe above (small,
   proven, exercises M2). Multi-ink layers wait for L2.
2. **Rolls folder:** `Licium\presets\bandit\` subfolder.
3. **Overlay lifetime:** corner ID persists until the next preset change.
