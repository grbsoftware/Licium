# Latent growth design — pheromone + the promotion loop (DRAFT)

*Status: **DRAFT — awaiting Gary's blessing.** Nothing here is implemented;
no code until the Decisions section is resolved. Parent doc:
`LATENT_DESIGN.md` (v1, blessed 2026-07-17), whose growth-path section
named these items as "post-v1, separate blessings" — this is that blessing
request for the first two: **pheromone** and the **promotion loop**.
Trigger: the first real Phase C session happened (2026-07-17 night — 33
rolls voted, 11 likes) — the loop finally has food.*

## What this milestone is

Latent v1 samples chains uniformly: every scan is a cold prospector with no
memory of what Gary liked. This milestone closes the loop the v1 doc
promised: **verdicts and words flow backward into later runs.**

Two features, built in order:

1. **Pheromone** — Phase B verdicts and Phase C votes deposit weight on
   grammar links; later scans *surface* chains that walk well-trodden
   liked paths (and bury blowout paths).
2. **Subtree miner** — a report that digs through the liked rolls' decoded
   chains for recurring fragments, ranked by how enriched they are vs the
   general passer population. The report is the raw material for fragment
   promotion (naming a fragment, making it a vocabulary token) — promotion
   itself is gated behind a look at the first real report (decision 2).

## The determinism problem (the design's spine)

The v1 ID contract: `LX1-XXXXXXXX` → seed → tree → emitted `.milk`,
byte-identical forever. Naive ACO breaks this — if pheromone biases the
*growth* of trees, then seed → tree depends on the pheromone state at scan
time, and an ID alone no longer regenerates its roll.

So the core design choice (decision 1) is **where the learning acts:**

- **Rank-side (RECOMMENDED):** seed → tree stays pure LX1 — pheromone
  never touches `BuildFromSeed`. Learning acts on the *ranking*: after
  screening, each passer's chain is scored against the pheromone table and
  the score biases which passers get emitted as `.milk` (the queue records
  both raw and pheromone scores). The same seed always regenerates the
  same file; what changes between runs is which seeds *surface* out of the
  millions. Learning = attention, not mutation. **No genver bump; LX1
  continues.**
- **Generation-side (true ACO):** pheromone biases token choice during
  growth. Stronger convergence, but seed → tree now depends on run state:
  requires an LX2 lineage where every scan archives its pheromone snapshot
  in a run manifest, and regeneration = seed + manifest. Heavier
  bookkeeping, and it *narrows the search* (uniform sampling was the v1
  doc's "proven first haul" choice for a reason). Better saved for when
  rank-side learning demonstrably starves (i.e., scans stop surfacing
  anything new that Gary likes).

## Pheromone — mechanics (rank-side version)

- **Links:** a chain decodes to (parent-op, child-slot, chosen-token)
  transitions — those triples are the pheromone keys, plus a few
  whole-tree features that verdicts already proved matter (lens, decay
  band, node-count band).
- **Deposits (derived, not stored — see decision 3):** at scan start the
  prospector reads the ground-truth files (`latent_votes.txt`,
  `latent_verdicts.jsonl`, `latent_tags.txt`), decodes each rated ID's
  chain (deterministic — the IDs regenerate their trees), and accumulates:
  like **+3**, latebloom-alive **+1.5**, alive **+1**, pass **−0.5**,
  frozen/black **−1**, blowout **−1.5** per link walked. (Weights are
  starting values; tuned against the first biased scan and recorded as doc
  amendments, L1-tuning convention.)
- ***Amendment 2026-07-18 — the re-audition rule (Gary's, with live proof).***
  A vote judges **formula × dressing**, not the formula: when the 20 good
  batch-2 seeds were re-dressed LX1→LX2, 4 of Gary's 8 likes flipped to
  passes and one pass flipped to a like — same expression strings, different
  clothes, different verdicts. His stated wish: "the ones I vote no on get a
  pass with another expression + test — stubs aren't a test of a string
  working." So NO/pass votes are **soft on first audition**: (a) a formula
  pass-voted in only ONE dressing enters the **re-audition queue** — the
  next scan re-emits it with a re-rolled dressing (different palette
  advance + re-rolled lens; medium too if decision 4 lands) alongside the
  fresh haul, so it gets a second look in new clothes; (b) the pass
  deposit (−0.5) lands only after a formula has been passed in **two**
  different dressings; one audition = no negative deposit at all;
  (c) a like in ANY audition counts fully (+3) and cancels the queue —
  positives are the scarce signal and are never discounted. Re-audition
  IDs carry a dressing-variant suffix (exact format settled at
  implementation and recorded here; the seed part stays the pure 32-bit
  formula id). Verdict-based deposits (alive/blowout/frozen) are about the
  field itself and stay single-shot.
- **Scoring:** passer's pheromone score = mean link weight along its
  chain, squashed; final rank = `raw_score × (1 + λ·pheromone)`, λ ≈ 0.5
  to start. A floor of unbiased picks is reserved (see next point).
- **Exploration floor:** every scan's emit budget reserves ~25% for the
  TOP RAW-SCORE passers ignoring pheromone (the v1 uniform behavior), so
  the prospector can never collapse into only re-serving Gary's known
  taste. (The latebloom lesson generalized: the best thing in a haul is
  often the thing the filter would have buried.)
- **Reporting:** scan summary prints how much the bias reordered the emit
  set (e.g. "14 of 40 emitted rolls were pheromone-promoted") so the
  effect is visible, not mystical.

## Subtree miner

- New prospector mode `--mine`: reads votes/verdicts, decodes chains,
  enumerates subtrees (depth 2–4), and reports fragments **enriched** in
  liked rolls vs the passer baseline (frequency ratio + occurrence count
  + which liked IDs carry it + the fragment's infix formula).
- Output: `Licium\latent_mine_report.txt` — human-readable, sorted by
  enrichment. Nothing automatic happens with it in this milestone: Gary
  reads the report next to the rolls he liked, and fragment *promotion*
  (naming one, adding it to the vocabulary as a locked-earned token,
  GENVER → LX2 since it changes seed → tree) is its own follow-up
  blessing with real candidates on the table.
- Notes/tags join the report: a fragment's carrier IDs bring their tags
  and descriptions along, so naming candidates starts from Gary's own
  words (the v1 doc's "descriptions become the names of new ingredients").

## What stays parked (explicitly not this milestone)

Behavior descriptors + MAP-Elites; Phase B idle-detect; mutations 2–4
(constants / fragment splices / binding swaps); the Modes dropdown; the
taste model (`LATENT_SPACE_ML_DESIGN.md`); Bandit-vocabulary feedback.
Each stays a separate blessing per the v1 doc.

## Tooling shape (no engine changes at all)

Everything lands in `tools\latent_prospect\` (the scan already reads no
engine state; it gains readers for the three ground-truth files) — the
engine, browser, and DLL are untouched. Zero gating risk: this milestone
cannot destabilize the app Gary uses.

## Verification plan

1. Determinism regression: same seed → byte-identical `.milk` before and
   after the pheromone build (proves rank-side purity); `--selftest` still
   passes.
2. Derivation determinism: two `--mine`/scan runs against the same
   ground-truth files produce identical weights, identical reports,
   identical emit sets.
3. Bias sanity: with the real vote files, a scan's emitted set differs
   from the unbiased set (reorder count > 0) AND the reserved unbiased
   floor is present.
4. Negative control: with empty vote/verdict files the scan reproduces
   v1's exact emit set (λ has nothing to act on).
5. Miner sanity: the report's top fragments each list ≥2 liked carrier
   IDs; every listed fragment's infix appears in the decoded formula of
   its carriers.
6. Live: a fresh biased scan's haul goes through Phase B + a Gary X
   session — the real acceptance test is whether the like-rate on
   pheromone-promoted rolls beats the unbiased floor's. (Not provable
   headlessly; stated so the goal is on record.)

## Decisions for Gary (bless or amend)

1. **Where learning acts** — RECOMMENDED: rank-side (LX1 continues, IDs
   stay pure, learning = attention). *Alternative:* generation-side ACO
   with LX2 + per-run pheromone manifests — stronger but narrower and
   heavier; revisit when rank-side starves.
2. **Milestone breadth** — RECOMMENDED: pheromone + miner REPORT only;
   actual fragment promotion (vocabulary token, LX2 bump) waits until the
   first report exists and a fragment is worth naming. *Alternative:*
   include promotion end-to-end now (bigger bite, and it would bump the
   lineage before the report proves any fragment deserves it).
3. **Pheromone storage** — RECOMMENDED: stateless — weights derived fresh
   each run from the vote/verdict/tag files (ground truth stays the only
   state; nothing to corrupt, evaporation emerges from later-wins votes).
   *Alternative:* a persistent pheromone file with explicit evaporation —
   classical ACO, but a second mutable source of truth to keep honest.
4. ***(Added 2026-07-18)* Medium variety — "the land of swirls."** Gary,
   after rating the whole batch-2 haul: every roll swirls, because the
   scaffold's advection medium is a FIXED swirl — the exact failure mode
   Bandit L1 paid for (one field formula → every roll the same comma-spiral;
   fixed by rolling the archetype). RECOMMENDED: roll the MEDIUM from a
   side-hash too (swirl / cell lattice / meandering stream / drift-weave,
   the proven Bandit four), in this same milestone (it is emitter-side
   dressing — the CPU screens judge the ink field and are untouched;
   genver bumps once more, and re-auditions then re-roll medium as well,
   which makes the second audition a genuinely different stage for the
   same actor). *Alternative:* keep the fixed swirl for lineage stability
   and take medium variety in a later pass — smaller step, but every scan
   until then keeps auditioning every formula in the same swirling room.
