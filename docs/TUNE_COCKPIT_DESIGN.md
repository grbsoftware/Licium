# Tune cockpit design — knobs on a rolled preset (DRAFT)

*Status: **DRAFT — NOT blessed, NOT scheduled.** Spitballed with Gary
2026-07-22; he does not expect to build this week. Faithful capture of the
conversation; every "decision" below is a proposal, not settled.*

## The itch

Gary loves the mangled rolls but "some flash violently" and some read like a
"color cycler." The plot (the buffer math / movement) is what the search
found — he does **not** want to touch that. He wants **knobs to shape the two
dressing layers on top of an existing roll — color, and how the music hits —
until a cursed roll lands in a sweet spot.** "That roll looks cursed but it
could be an interesting glitch if it was tweaked a little."

Diagnosis that anchors the design: rolls drive audio almost entirely onto
**ink intensity + the comp light vector** (brightness / palette position),
never warp geometry — and bass/mid/treb are relative + pinned near 1.0 in
steady sound. So on every beat the palette ramp jumps and the field flashes.
That "color cycler / violent flash" is the target of the Music knobs.

## Core architectural choice (proposed, strong)

**Tuning is a reversible overlay stored in a per-roll sidecar, NOT baked into
the .milk.** Same family as the existing ground-truth sidecars
(`latent_votes.txt`, `latent_tags.txt`, `latent_notes.txt`). New file:
`latent_tunings.txt` (`ID<TAB>hue sat light contrast decay gain smooth ...`),
later-wins. Re-applied on load. Rationale:

- Non-destructive — the original roll is never overwritten; snap any knob to
  center = untouched. (Center = "as rolled" for every knob.)
- Some knobs (freeze the reaction, 50% smoothing) have **no .milk field to
  bake into** — they only exist as an engine-side overlay, so the sidecar is
  the only home for them.
- The tuning **travels with the ingredient**: when Bandit later draws from a
  liked roll (see the promotion story in [[milkdrop-md2-port]]), the sidecar
  tuning can travel too. (Future; v1 scope = the browser.)

## Two implementation tiers

- **Color knobs = cheap.** Extend the proven recolor-in-place path (`P` already
  does this for liked palettes — see `licium_latent.cpp` `CyclePalette` /
  `RecolorRollFile`). Transform the roll's OWN palette literals (HSL/contrast),
  re-emit to a throwaway `.milk`, hard-cut reload. No engine rebuild. Keyboard
  +/- nudges = discrete steps, which suits a keyboard-driven browser anyway.
- **Music knobs = one engine hook.** The host scales & smooths `bass/mid/treb`
  (+ their `_att`) *before the shader reads them*, using live knob values.
  Nothing baked into the .milk. Standalone-gated, small — matches the project's
  pattern of standalone engine hooks.
- **Movement target = deeper (defer).** Rolls barely drive geometry today, so a
  `beat → movement` link doesn't *tweak* motion — it *adds* reactive motion the
  roll never had. Great capability (a live taste of rung-2/3 growth) but a
  meatier hook. Sequence it after color/smear/reactivity.

## The knobs (starter set — Gary explicitly expects to keep adding)

**Color** *(recolor path)*
- hue rotate · saturation · light/exposure · contrast
- **gradient smoothness curve** — a spline whose x = position along the palette
  (deep → accent), y = how fast one color gives way to the next; flat middle =
  hard bands, s-curve = soft dissolve. Nonlinear. Lives in the Color dialog.

**Motion** *(engine hook)*
- smear (decay) · gain (0 = freeze to a still, 1 = as-rolled, >1 = wilder) ·
  smooth (attack/decay envelope — THE anti-violent-flash knob) · band tilt.
- "gain = 0" freeze doubles as a **voting aid** — judge the static composition
  without the strobe fighting you. Possibly worth shipping alone, early.

**React** *(source → target routing + response splines)*
- Sources (all cheap, from the FFT the engine already runs): **beat · volume ·
  bass · mid · treble · brightness(centroid)**. (Pitch-as-one-number = the
  spectral centroid: `Σ i·energy / Σ energy` — a single low↔high dial. Honest
  but it *averages* — two notes far apart put the centroid in the empty middle.
  So the real power is region *routing*, not a single pitch scalar. The full
  spatial/temporal version is the audio-texture layer — see companion doc.)
- Each link: pick a source, pick a target (movement / smear / color), set an
  amount, and shape a **response spline** (nonlinear input→output curve): a flat
  start ignores quiet passages, a steep end punches on peaks.
- This is the "earmark where the sound comes from" idea made literal:
  bass→movement, mid→smear, treble→color, each with its own curve — sidesteps
  the centroid's "average lies" problem because you never averaged.

## Interaction spec (from Gary, captured)

- Every knob = **slider + editable number field**. Number is displayed
  normalized (NOT the raw shader value). Slider commits the number on release;
  typing in the number snaps the slider live. Two-way. (Sliders alone are hard
  to fine-tune — the number field is the fix.)
- **Not an always-on panel.** A sidebar button opens a **floating dialog**;
  configure, then dismiss. Knobs cluster under a few buttons with obvious
  grouping. Win32 modeless dialog — proven pattern (`licium_settings.cpp`,
  `IDD_LATENT_SEARCH`). A spline is an owner-drawn child control.
- Dressed-up dark chrome to match the canvas (Gary: "we can dress the dialogue
  up").

Proposed sidebar buttons (obvious grouping, each its own floating dialog):

| Button | Holds |
|--------|-------|
| Color | hue · sat · light · contrast + gradient-smoothness curve |
| Motion | smear (decay) · gain · smooth · band tilt |
| React | source→target links + response splines |

## Visual-design notes (from the mockups)

- Earlier minimalist on-canvas idea (thin stacked rows, one knob per row, dark,
  single-glyph labels, center tick = "as rolled") is superseded by the
  floating-dialog direction, but the aesthetic (very dark, very quiet, only the
  active control lights up) carries into the dialog chrome.
- Number fields right-aligned mono; sliders thin; group headers, not per-row
  borders.

## Decisions (Gary, 2026-07-22 — still DRAFT overall, but these are settled)

1. **Build order: cockpit FIRST, on cheap scalar sources.** React uses the
   scalars the engine already computes (beat/volume/bass/mid/treble/centroid).
   The audio-texture layer (companion doc) is a LATER upgrade that makes React
   spatial/temporal — not a prerequisite.
2. **Movement target = DEFERRED.** First cockpit ships Color + Motion
   (smear/gain/smooth) + React on smear/color targets. `beat→movement` (the one
   deeper hook) comes after the cheap knobs prove the UX.
3. **First slice to build = COLOR knobs** (hue/sat/light/contrast + gradient
   curve). Cheapest — extends the existing `CyclePalette`/`RecolorRollFile`
   recolor path, no engine rebuild — and directly tames the "color-cycler" look
   for the fastest visible result. Motion knobs (which need the att-scaling
   engine hook) follow, then React routing.

## Still open (build-time details, my call unless Gary weighs in)

- Real pitch/note tracking vs. centroid-as-pitch — centroid is cheap and honest
  for a single dial; region routing is preferred over both. (Leaning: ship
  centroid + region sources, skip note tracking.)
- Sidebar button set + exact key to summon the cockpit (consult
  `docs/HOTKEYS.md` before binding).

Companion doc: `AUDIO_TEXTURE_DESIGN.md` (the invisible layer React would tap).
