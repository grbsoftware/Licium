# Latent design — Licium clean-room re-port (v1)

*Status: DRAFT — awaiting Gary's blessing. No code until blessed. Heritage:
`MilkdropProject\LATENT_SEARCH_DESIGN.md` (settled 2026-07-12) plus the
lessons paid for during the old tree's LS1/LS2 builds. Companion (future):
`LATENT_SPACE_ML_DESIGN.md` (taste model — not this doc).*

## What Latent is

An automated prospector that writes brand-new shader formulas nobody
designed, filters them by machine, and presents the survivors in Licium for
you to vote on, tag, and DESCRIBE. Descriptions become the names of new
ingredients that later feed the bandit's vocabulary — we are making
ingredients for behaviors. It is the third tool pillar
(Bandit / Palettes / **Latent**).

Three phases at different cadences, queues between:

- **Phase A — structure prospector** (offline CPU tool, overnight-scale):
  builds candidate formulas as chains through a typed grammar, so everything
  compiles by construction. Static screens + dedup rank a queue; the top of
  the queue is emitted as real `.milk` rolls.
- **Phase B — automated eye** (script + the real renderer): loads each roll,
  grabs frames, verdicts black / frozen / blowout / alive. Kills nothing —
  verdicts are labels, not deletions.
- **Phase C — the human, in Licium** (press **X**): browse survivors live,
  like/pass, quick-tag, describe. Verdicts and words flow backward into
  later runs.

## Clean-room rule (hard constraint)

The old Latent build (prospector, triage script, in-app browser) lives with
the closed 3.33-contaminated tree and its environment. **Nothing from there
is opened, diffed, or copied — including the standalone `env\latent\` tools,
which are rewritten fresh inside this repo** (uniform provenance: every line
of Licium traces to this tree's docs — see decision 2). The spec is: this
document + the settled heritage doc + remembered lessons. All engine-side
code is `MILKDROP_STANDALONE`-gated; the Winamp DLL stays pristine.

Carried-over *knowledge* (paid for in the old tree):

- **Lenses, not clipping.** `saturate(f)` kills all negative structure —
  dark rolls were hiding real behavior. Each seed hashes one of three field
  lenses: `sat(f)` (45%) / `sat(|f|)` (30%) / `sat(1-|f|)` zero-crossing
  glow (25%), applied IDENTICALLY in the CPU screens and the emitted shader.
- **Late bloomers are real.** Some rolls look dead for 30s then develop.
  Phase B quick-look failures get a 40s-settle second look; alive-after-
  development = verdict `latebloom`, treated as alive.
- **Blandness is mostly scaffold, not formula.** Rolls are dressed from the
  curated palette library as a luminance-sorted 3-stop ramp (deep×0.30 /
  mid / accent); the evolved tree itself stays colorless — don't
  un-sanitize the building blocks.
- **Emit cap ≠ discard.** Every screened candidate stays in the ranked
  queue file, every seed regenerates. Rejected = deprioritized, never
  destroyed (glitch shelf, not a trash can).
- **Engine facts:** bass/mid/treb ARE bound in `bufN_` passes; "Error
  parsing X shader" means the shader_body text surgery failed (usually a
  brace), not a compile error; silence pins bass/mid/treb to 1.0, so
  must-dance is screened on the CPU and confirmed live, never from stills.

## The settled design (2026-07-12 — not reopened here)

- **Vocabulary launches LEAN — force invention.** Arithmetic atoms
  (`+ - * / abs frac floor min max`, comparisons) plus only the structural
  essentials: uv/position, time, audio uniforms, feedback-buffer read.
  `sin/length/dot/noise` stay locked until earned (fragment promotion) or
  deliberately unlocked later.
- **Grammar chains, not vector search.** Typed grammar; at each link only
  legal tokens are offered, so compilability is the entry ticket, not the
  filter. A chain serializes to a number list; the ID fully decodes to the
  shader byte-for-byte.
- **First evolvable slot: the ink field** on a fixed template. Guardrails
  (equilibrium clamp `gain/(1-decay)`, tonemap, depth cap) live in the
  TEMPLATE, never in the evolved tree — the mutant cannot blow up the
  feedback loop.
- **Nothing is ever deleted.** Landing pages for modes, with
  "don't show again" + a reset path. No external shader ingestion, ever.

## V1 scope — the proposed slice

**Phase A** — `tools\latent_prospect` (small C++ CLI, built by the repo,
no engine dependency):

1. Seed (32-bit) → LCG (Bandit's fixed spec, own stream) → typed expression
   tree over the lean vocabulary, depth-capped.
2. CPU interpreter screens each candidate: instruction budget, uses-audio +
   audio-influence (must-dance), 2D variance on both axes (kills stripes/
   washes), look-space hash dedup (FNV over a coarse sample grid — dedup by
   what it LOOKS like, not how it's written).
3. Survivors ranked into `queue.jsonl` (all of them, forever); top K
   (cap 200) emitted as `Licium\presets\latent\latent_LX1-XXXXXXXX.milk`
   on the scaffold: buf0 feedback medium template (M2 engine) with
   equilibrium clamp, per-seed lens, palette ramp from
   `licium_palette_lib.h` (the header is already standalone/includable).

**Phase B** — `tools\latent_triage.ps1`: launches Licium detached, loads
each untriaged roll, Spout-grabs, verdicts `black | frozen | blowout |
alive`, with the 40s two-look pass for `latebloom`. Appends to
`Licium\latent_verdicts.jsonl`; writes a stamped contact sheet. Manual
trigger only in v1 (idle-detect is growth path).

**Phase C** — in Licium, `X` (or menu: Latent > Explore):

1. First open shows the **landing page** (what this mode is, keys), with
   "don't show again" persisted to `licium.ini`; Help menu gets
   "Show intro pages again".
2. Browser lists rolls sorted alive/untriaged/glitch-shelf; arrows load
   the selection LIVE (hard cut); modal like the wall — eats all keys,
   auto-advance held (the Palettes hold covers any host modal).
3. Votes: `Y`/`Space` like, `N` pass, `U` clear →
   `Licium\latent_votes.txt` (append, later-wins). Quick-tags `1`–`6` =
   organic / geometric / glitchy / watery / sparkly / ??? →
   `latent_tags.txt` ("???" is a deliberate signal). `D`/`Enter` opens a
   text field (engine waitstring) → description into `latent_notes.txt`
   (tab-separated). `X`/`Esc` close.

Not in v1 (growth path): pheromone/ACO link learning (v1 samples chains
uniformly — the proven first haul), behavior-descriptor novelty +
MAP-Elites archive, promotion loop (subtree mining + ablation rig),
fragment/binding/constant mutations, Modes dropdown unifying V/X, taste
model, idle-detect.

## IDs and reproducibility

- **Format `LX1-XXXXXXXX`** — Licium latent lineage, genver 1, 32-bit seed
  in hex (chains need more entropy than Bandit's 16-bit knobs). Old-tree
  `LS1-`/`LS2-` IDs are a different lineage and never interchange (same
  rule as Bandit's `L1` vs old `5-`…`26-`).
- Same LCG spec as Bandit/Palettes (`licium_rng.h`, already refactored out
  and byte-verified) — seed → tree → emitted `.milk` must be byte-identical
  forever. GENVER bumps on any grammar/template/draw-order change.
- Acceptance test: same seed twice → byte-identical file; an ID decodes to
  its chain (number list) with a `--decode` flag on the prospector.

## Engine integration (all standalone-gated)

- **New files:** `vis_milk2\licium_latent.cpp/.h` (browser state, votes/
  tags/notes files, landing page) — compiled only into the standalone
  vcxproj. `tools\latent_prospect\` (CLI + shared grammar/interpreter
  headers; the emitted-template text lives here so prospector and repo
  stay in one place). `tools\latent_triage.ps1`.
- **Keys:** `x`/`X` binds through `HostHandleChar` (built for this);
  arrows/PgUp/PgDn through `HostHandleKeydown`; Space keydown swallowed
  while open (the wall's lesson). `x` is free per `docs\HOTKEYS.md` — bind
  it and update the registry + F1 text in the same commit.
- **Menu:** the greyed **Latent** heading becomes: *Explore (X)* ·
  *Open rolls folder* · *Open notes file*. Existing host WM_COMMAND path.
- **Render:** quads via the engine device + labels via `m_text`, from the
  gated UI-render hook (raw DrawText renders nothing — twice confirmed).
- **Waitstring gotcha:** description entry reuses the engine's waitstring;
  the browser's own labels must yield while it is active (old collision
  lesson), and synthetic WM_CHAR doesn't reach it — text entry is verified
  by hand.
- **Automation hook:** `WM_APP+0x53`: wParam=1 open browser / 0 close;
  navigation and votes drive through posted keydowns (physical-key
  mimicry). Mirrors 0x51 (Bandit) / 0x52 (wall).

## Verification plan

1. Both projects build; vis_milk2.dll stays pristine (gating proof).
2. Prospector determinism: same seed twice → byte-identical `.milk`;
   `--decode` round-trips the chain.
3. A real run: ≥100k seeds screened; emitted rolls load in-engine without
   shader errors (compile-by-construction proof); queue.jsonl retains all
   passers.
4. Phase B headless: verdicts land in the jsonl; a known-black seed
   verdicts black; contact sheet renders; second-look latebloom path
   exercised.
5. Browser headlessly: open via 0x53, grab shows list + live roll; posted
   votes/tags append correct later-wins lines; auto-advance held while
   open; description typed by hand lands in latent_notes.txt.
6. Landing page: shows once, checkbox persists, Help-menu reset re-shows.
7. HOTKEYS.md + F1 updated in the binding commit.

## Growth path (post-v1, separate blessings)

- **Pheromone** on grammar links, deposited by Phase B verdicts + Phase C
  votes (the ACO/Flocker family move).
- **Behavior descriptors + MAP-Elites** archive (novelty = distance in
  look-space); Phase B idle-detect.
- **Promotion loop:** mine liked survivors for recurring subtrees, ablation
  in a test-rig scaffold, name from Gary's descriptions, feed back into the
  grammar AND Bandit's vocabulary (open-endedness).
- **Mutations 2–4:** constants, fragment splices, binding swaps (the
  MIDI-patch move).
- **Modes dropdown** unifying V/X landing pages; taste model per
  `LATENT_SPACE_ML_DESIGN.md`.

## Decisions for Gary

1. **V1 breadth** — RECOMMENDED: all three phases in one milestone, but
   implemented and committed as two checkpoints: M5a = Phase A + B
   (offline; rolls already loadable from the normal preset list), pause for
   a look, then M5b = the X browser. Each half is independently useful and
   the old tree proved both.
2. **Old standalone tools** (`env\latent\prospect.cpp`, `phaseb.ps1` — not
   MD3 source, but built alongside the closed tree) — RECOMMENDED: rewrite
   fresh in `MD2-port\tools\`, don't copy. They're small, this doc carries
   their lessons, and it keeps the whole public repo's provenance one
   sentence long: "everything here was written from the docs in this tree."
3. **ID lineage** — RECOMMENDED: `LX1-XXXXXXXX` (new prefix; old-tree
   LS1/LS2 stay a foreign lineage, same as Bandit's L1 vs 5–26). Old
   discoveries stay browsable in the old environment; they don't migrate
   into the repo.
