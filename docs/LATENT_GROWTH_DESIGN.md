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

## What stays parked (explicitly not this milestone)

Pheromone (rank-side AND generation-side — retired to the shelf unless
vote volume ever justifies it); behavior descriptors + MAP-Elites; Phase B
idle-detect; constant/binding mutations; the Modes dropdown; the taste
model (`LATENT_SPACE_ML_DESIGN.md`).

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
