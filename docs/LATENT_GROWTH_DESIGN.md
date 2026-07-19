# Latent growth design — cold prospector, stub pile, and the Bandit gate (DRAFT)

*Status: **DRAFT — awaiting Gary's blessing** on the open decisions at the
bottom; the architecture itself is his (2026-07-18 direction, superseding
this doc's first pheromone-centric draft). Parent doc: `LATENT_DESIGN.md`
(v1, blessed 2026-07-17). No code until blessed.*

## The architecture (Gary, 2026-07-18)

Three laws, from the horse's mouth:

1. **Latent prospects COLD, every time.** Uniform sampling at
   ~10M-seed scale per scan (hundreds of thousands of passers into the
   ever-growing queue). Votes are KEPT — they are the permanent taste
   record — but ~40 votes a session is statistical dust against that scan
   scale, and biasing generation or ranking with it would only narrow the
   search. (This retires the first draft's rank-side pheromone: parked in
   the growth path, revisit only if vote volume ever rivals scan volume.)
   ***Amendment, same evening:* swarm REVIVED as an experimental second
   mode** — Gary, recalling the heritage doc's ACO core ("we did swarm
   intelligence in latent"): *"let's build swarm into this and see what it
   does."* Cold remains the default cadence; `--swarm` is the experiment
   beside it. **BUILT 2026-07-18** — see the Swarm mode section.
2. **Votes dictate what flows to BANDIT.** The taste signal's real job is
   downstream: liked latent discoveries are mined, named, and promoted
   into Bandit's vocabulary as new ingredients/behaviors. Latent is the
   mine; Bandit is the jewel box.
3. **Promotion is GATED — no behavior may flood Bandit.** The swirl lesson
   at the promotion layer: every promoted behavior gets a mathematically
   capped share of Bandit's roll distribution, so a hundred liked swirls
   still cannot make Bandit a swirl machine.

**The long game — doping the public version.** Gary keeps rating through
this curation phase; the accumulated promotions become the interesting-
behavior starter set the PUBLIC build ships with. Early on the effect on
Bandit will be small and rolls will stay "slightly boring" — that is
expected and fine; the pile and the promotions compound.

## What this milestone builds

1. **Scale + hygiene for cold prospecting** — the scan is already cold;
   make the 10M-seed overnight run the standard cadence (perf pass if
   needed, idle-priority launcher, queue growth is by design).
2. **The stub pile** (Gary's rule, unchanged from the amendment below).
3. **The subtree miner** — mines LIKED rolls for recurring fragments; its
   report is the promotion pipeline's front door.
4. **The Bandit gate (design here, implement with Bandit L2 or after):**
   the promotion path and its influence caps.

## The stub pile (Gary's rule — settled, not a decision)

- **A pass-voted formula is never written off.** No negative taste deposit
  of any kind, ever — "leave no stone unturned."
- A pass-voted formula goes on the **stub pile**. Each return visit grows
  **one new expression layer** on it (the stub's tree becomes a subtree
  under fresh grammar growth from a revisit seed), re-dresses it, and
  sends it back through screens → Phase B → the browser like any fresh
  roll. Voted no again → back on the pile, one layer deeper next time,
  "until something works."
- The pile is unbounded by design; when it is huge we return to it in
  batches (a revisit budget rides alongside each fresh scan's emissions).
- **A like at any depth counts fully** and hands the miner a beautiful
  specimen: a working outer layer wrapped around a known-struggling core.
- ID lineage: a regrowth regenerates from (parent formula id, layer count,
  revisit seed); exact format settled at implementation and recorded here.
- Evidence this is right (2026-07-18): re-dressing LX1→LX2 alone flipped 4
  of 8 likes to passes and one pass to a like on identical formula strings
  — a vote judges the whole presentation, so a formula deserves unlimited
  new presentations.

## The subtree miner

- Prospector mode `--mine`: decodes every LIKED roll's chain, enumerates
  subtrees (depth 2–4), reports fragments **enriched** vs the passer
  baseline: frequency ratio, carrier IDs, the fragment's infix formula,
  and the carriers' tags/notes (naming candidates start from Gary's own
  words — "descriptions become the names of new ingredients").
- Output `Licium\latent_mine_report.txt`, human-readable. Nothing
  automatic: the report is where Gary picks what gets NAMED and promoted.

## The Bandit gate (the new centerpiece)

The promotion path, end to end:

1. **Mine** — the report surfaces candidate fragments from liked rolls.
2. **Name** — Gary picks winners and names them (his descriptions/tags).
3. **Promote** — a named fragment becomes a Bandit-side ingredient (a new
   entry in an ingredient table Bandit's generator draws from; genver
   bumps on Bandit's side when the table changes).
4. **Gate** — the caps (the design's teeth):
   - **Global guest share `G`:** promoted-from-latent ingredients
     collectively influence at most `G` of Bandit rolls (a seeded draw
     decides per roll whether any guest ingredient participates at all).
     Suggested start: `G = 25%`.
   - **Per-behavior cap:** within the guest share, no single promoted
     behavior exceeds a fixed fraction (suggested: equal split, hard cap
     `G/2` even when only two behaviors exist) — a monoculture cannot
     form mathematically, however many swirls get promoted.
   - **Family grouping (the swirl clause):** behaviors judged same-family
     (by Gary at naming time — e.g. "this is another swirl") share ONE
     family slot in the split, not one each. Liking a hundred swirls
     grows the swirl family's QUALITY (best exemplar wins the slot or
     rotates), never its SHARE.
   - Caps are constants in the emitted-generator spec (genver-relevant,
     documented, tunable only via doc amendment).

## Swarm mode — BUILT (2026-07-18, generation-side ACO)

`latent_prospect --swarm <LiciumStateDir> [--lambda F]`, the heritage
doc's ant-colony core made real:

- **Pheromone table** `(parent kind, child slot, child kind) → weight`,
  derived STATELESS at scan start from `latent_votes.txt` +
  `latent_verdicts.jsonl` (later-wins): like **+3**, latebloom **+1.5**,
  alive **+1**, frozen/black **−1**, blowout **−1.5**; pass votes deposit
  **nothing** (the stub-pile law holds in swarm too).
- **Bias acts on the kind draw during growth** (`BiasedPick`): weights
  `1 + λ·w` clamped, floor 0.05 so nothing reaches zero probability
  (exploration floor). Exactly one RNG draw per node either way, and a
  flat context reduces to the EXACT cold arithmetic — **a swarm scan over
  an empty state equals the cold scan tree-for-tree** (verified). Class
  split (45/20/35) stays structural, unbiased.
- **Lineage `LXS1-XXXXXXXX`**: seed alone no longer determines the tree,
  so each swarm run archives its table as `swarm_manifest_<start>_<count>`
  next to the queue — regeneration = seed + manifest; the `.milk` files
  remain the permanent artifact. Foreign/LXS ids are skipped during
  derivation (reported, never guessed). `--decode` stays cold-only.
- **Multithreaded** like the cold scan (table is read-only during growth).
- **Verified 2026-07-18:** selftest PASS; cold path byte-identical after
  the refactor; empty-state = cold invariant; real-state (92 rated ids)
  deterministic across runs, manifest written; screen-pass rate rose
  2316 → 2870 on the same 30k range — the bias measurably steers toward
  structures that survive screens.
- **The experiment:** run swarm hauls beside cold hauls, triage both, and
  let Gary's like-rate judge — does concentrating near his taste beat
  uniform coverage, or does it just breed comfortable sameness? The gate
  (previous section) protects Bandit either way.

## The expedition mixer (Gary, 2026-07-18 — public-version concept, parked
## here so it isn't lost; its own design pass later)

Gamified compute allocation: the user points percentages of a scan budget
at different search methods — "100% thread hunting," or "40 cold / 30
swarm / 30 threads" with load balancing. Mechanically it is just a
scheduler slicing the seed budget into sub-scans (each one an ordinary
run: own lineage rules, own manifest, all determinism intact), so the
engine cost is a UI, not a search rewrite. Why it's right for the public
build: (a) it makes prospecting an identity (thread hunters, swarm
riders, deep-field purists); (b) every user's allocation + like-rate is
calibration data for the shipped default mix; (c) it IS the Bandit doc's
parked "Expeditions" item — a named allocation + seed budget + results
folder = an expedition. Symmetry worth keeping in the pitch: compute
shares steer where the search LOOKS, the promotion gate's influence
shares steer what findings may DO — allocation sliders at both ends of
the pipeline. Needs: in-app launcher for the prospector (or embedding),
progress UI, per-method result tagging in X. Design doc when its turn
comes; the current cold/swarm/seek experiments are the data that
calibrates it.

## GPU screening (Gary, 2026-07-18 — approved direction, build after the
## cold/swarm/seek experiment lands)

Raw-compute offload for the screens, ~10–20x on top of the CPU threads
(his card: GTX 1660 Ti, ~1536 cores; driver reports CUDA 11.1, not 13.x).
Decisions taken in discussion:

- **D3D11 compute shaders, not CUDA** — vendor-neutral (NVIDIA/AMD/Intel;
  "even if they are using a different kind of card"), no toolkit install,
  works on his current 2020 driver, and the D3D compiler is already part
  of this project's world.
- **V1 role: GPU proposes, CPU decides.** The GPU runs the settle-screens
  as a mass REJECTOR with safety margins; passers and near-threshold
  candidates are re-screened on CPU exactly as today. Queue, scores, and
  rankings remain byte-identical CPU truth — GPU float drift can only
  cost a sliver of borderline candidates, never corrupt a result.
- **Bit-exact promotion path:** the lean vocabulary is accidentally
  GPU-determinism-friendly — add/sub/mul/div/min/max/abs/floor/frac are
  all IEEE-exact; no transcendentals. With FMA contraction pinned on both
  sides, bit-exact GPU screening is provable for this grammar; a large
  A/B zero-mismatch run would promote the GPU from proposer to referee.
- Build shape: flatten trees to postfix programs, batch thousands of
  candidates per dispatch, one thread per (candidate, cell), readback
  pass/fail + scores. Expedition mixer gains a gpu share knob when both
  exist.

## What stays parked (explicitly not this milestone)

Rank-side pheromone (superseded — generation-side swarm exists instead);
behavior descriptors + MAP-Elites; Phase B idle-detect; constant/binding
mutations; the Modes dropdown; the taste model
(`LATENT_SPACE_ML_DESIGN.md`).

## Verification plan

1. Determinism regression: `--selftest` byte-identical; a 10M scan twice
   over the same range produces identical queue lines (cold = pure).
2. Stub regrowth determinism: same (parent, layer, revisit seed) →
   byte-identical emission; a layered roll's ID decodes to parent chain +
   growth layers.
3. Miner sanity: every reported fragment's infix appears in its carriers'
   decoded formulas; carriers ≥ 2 for every top-10 fragment.
4. Gate math: over a large simulated roll count, guest participation →
   `G ± ε`; no behavior exceeds its cap; family grouping verified with a
   synthetic two-behaviors-one-family case.
5. Live: a doped Bandit session for Gary — the acceptance test is that
   rolls surface promoted behaviors at a noticeable-but-not-dominant rate.

## Decisions for Gary (bless or amend)

1. **Gate numbers** — RECOMMENDED: `G = 25%` guest share, equal split with
   `G/2` hard cap, family slots as specced. (Pure taste numbers — pick any
   you like; they live in the doc and are amendable.)
2. **Milestone breadth** — RECOMMENDED: stub pile + miner + 10M cadence
   NOW (all Latent-side, no engine changes); the Bandit gate implements
   together with Bandit L2 (it touches the same generator tables), using
   this doc's spec. *Alternative:* everything in one push.
3. **Medium variety — "the land of swirls."** RECOMMENDED: roll the
   MEDIUM from a side-hash too (swirl / cell lattice / meandering stream /
   drift-weave, the proven Bandit four) in the next latent genver — the
   scaffold's fixed swirl is the Bandit comma-spiral lesson again, and
   stub regrowths then return on a genuinely different stage each visit.
   *Alternative:* keep the fixed swirl for lineage stability; medium
   variety in a later pass.
