# SWARM_DIVERSITY_DESIGN — giving the swarm a job cold search cannot do

*Status: DRAFT, awaiting Gary's blessing. No code until then.*
*Written 2026-07-28. Supersedes the vote-pheromone swarm described in
LATENT_GROWTH_DESIGN.md, which is retired with evidence (see §1).*

---

## 0. The ask

Gary, 2026-07-28:

> *"I would like a valid use for swarm — does it HAVE to be based on likes? Can
> it be based on rules which are not necessarily my likes? ... I know we were
> kind of tuning it to my tastes; the problem is I haven't seen enough of what's
> out there to tell you what my tastes are."*

He wants a reason to keep the swarm that a person who knows swarms would call
smart — not a rationalisation for keeping a toy. This document is that reason,
or it is the document that admits there isn't one.

The short version: **the swarm's valid job is finding DIVERSITY, not finding
QUALITY — and that is a job cold scanning mathematically cannot do.**

---

## 1. Why the first swarm failed (evidence, not opinion)

Three-haul experiment, 2026-07-19, evidence commit `e655707`. Gary rated all 135
rolls blind to method:

| method       | like-rate    |
|--------------|--------------|
| thread-seek  | 15.0% (6/40) |
| cold         | 12.7% (7/55) |
| **swarm**    | **7.5% (3/40)** |

Swarm came in at roughly **half** of cold. The diagnosis is not that the ACO was
implemented badly — it is that it was pointed at the wrong objective:

- **The signal was dust.** ~40 votes per session against a 10M-seed space. Any
  pheromone table built from that is fitting noise.
- **The landscape is deceptive.** Reinforcing what is already liked walks the
  search *into* the region already sampled. This is textbook premature
  convergence, and it is exactly what a taste-gradient does on a search space
  whose good regions have not been located yet.
- **It optimised the thing Gary could not yet specify.** He says plainly he has
  not seen enough to know his own taste. An optimiser aimed at an objective the
  principal cannot state will faithfully optimise the wrong thing.

The retirement decision (`e3a89ce`, "cold prospector") was correct **given that
objective**. This document does not reopen it. It changes the objective.

---

## 2. The insight this whole design rests on

> **Cold scanning is uniform over SEED space. It is not uniform over BEHAVIOUR
> space. Those are radically different distributions.**

The map from seed to visible behaviour is extremely non-uniform: enormous
regions of seed space collapse onto the same small set of behaviours. Random
sampling reproduces that distribution faithfully — which means it keeps handing
back the crowded middle.

This is not speculation. It is the direct mechanical explanation for Gary's own
finding, the single most important sentence in the project's history:

> *"all have the same behaviors — I just rated Y on the ones that didn't bloom
> or go dark."*

Two consequences follow, and they are the crux:

1. **Scanning harder cannot fix this.** 1M seeds or 100M, the distribution is
   the same shape. Cold search has no memory of where it has already been, so it
   re-samples the crowded region in proportion to its size, forever.
2. **A swarm has memory.** An archive of where the search has already been is
   precisely the thing cold search lacks and a swarm provides. That is the
   honest, non-sentimental reason the swarm belongs in this project.

The monoculture is a *sampling* problem, not a *scaffold* problem. (The scaffold
is a separate, real ceiling — rungs 2-3 of the growth ladder. Both are true. This
document addresses only the sampling half, and the sampling half is cheaper.)

---

## 3. The method — two published techniques that stack

Neither of these is invented here. Both are established, cited work, and both
were designed for exactly this failure mode.

### 3.1 Novelty Search
Lehman & Stanley, *"Abandoning Objectives: Evolution Through the Search for
Novelty Alone"*, Evolutionary Computation 19(2), 2011.

The counterintuitive result: on deceptive problems, searching for novelty
**alone** outperforms searching for the objective, because the objective's
gradient leads into local optima. Novelty is scored as the mean distance to the
*k* nearest neighbours in an **archive of behaviours already seen**.

Critically for us: **it uses no human labels at all.** It answers Gary's question
("does it HAVE to be based on likes?") with a flat no, and with a citation.

### 3.2 MAP-Elites / Quality-Diversity
Mouret & Clune, *"Illuminating search spaces by mapping elites"*, 2015; Cully,
Clune, Tarapore & Mouret, *Nature* 521, 2015; Pugh, Soros & Stanley, *Frontiers
in Robotics and AI*, 2016.

Discretise behaviour space into cells. Keep the **best roll per cell**. The
output is not a ranked list — it is an **illuminated map of everything the system
can do**, one exemplar per niche.

---

## 4. Why this fits THIS project specifically

It resolves three separate open problems with one mechanism:

- **"I haven't seen enough to tell you my taste."** A MAP-Elites archive is
  literally a map of what is out there. It is the artefact that answers the
  question, rather than another sample from the crowded middle.
- **The attention wall.** Gary asked for "~50 representatives instead of 30k".
  MAP-Elites *is* that — derived from structure, not from post-hoc clustering of
  whatever happened to get emitted.
- **The monoculture.** Once a cell is occupied, novelty there falls
  automatically. **The swarm self-avoids the rut.** This is the precise property
  the vote-driven version lacked, and it is structural rather than tuned.

### The synthesis — where votes DO belong

The original mistake was not *using* votes. It was **using votes at the wrong
scale**:

| scale | vote signal | verdict |
|---|---|---|
| steering a 10M-seed global search | ~40 votes | statistical dust — this is what failed |
| choosing between two rolls in the SAME behavioural cell | ~40 votes | a valid paired comparison |

Two rolls in one cell have the same measured behaviour and differ only in
execution. That is a question Gary's taste can answer reliably, and it is the
only question it is being asked.

> **Diversity is machine-decided. Quality is Gary-decided. The two never touch.**

This also respects the never-delete law: nothing is pruned, cells are only ever
filled or improved.

---

## 5. PRE-COMMITTED behaviour descriptors

**These are fixed in writing BEFORE any run.** Choosing descriptors afterward to
make the map look good is the single thing that would invalidate the result, and
it is the most common way QD work goes wrong.

Nearly all of this is **already computed** by `ScreenCandidate` in
`latent_prospect.cpp`. No rendering, no human, near-zero added cost.

| # | descriptor | source (already exists) | perceptual meaning |
|---|---|---|---|
| B1 | **motion energy** | `flow` — mean curl-velocity magnitude, 8x8 | still ↔ churning |
| B2 | **granularity** | NEW: mean abs neighbour difference / stddev, 32x32 | broad shapes ↔ fine filaments |
| B3 | **anisotropy** | `\|rowVar-colVar\| / (rowVar+colVar)`, 32x32 | isotropic blobs ↔ directional / striped |
| B4 | **persistence** | `decay` (rolled per seed) | crisp ↔ trailing smear |
| B5 | **geometric audio reactivity** | `gdance / flow` (normalised) | does music bend the SHAPE |
| B6 | **ink audio reactivity** | `dance` | does music change WHERE ink is |
| B7 | **density** | `mean` of settled field | sparse ↔ full |
| B8 | **contrast** | stddev of settled field | flat ↔ punchy |

Only **B2** is new, and it is ~10 lines over a grid already in memory.

### Map axes vs archive dimensions

A MAP-Elites grid becomes unusably sparse above ~4-6 dimensions. So:

- **Map axes (the grid): B1, B2, B3, B4.** Chosen because they are the four most
  likely to correspond to something Gary can *see* in a still frame, and because
  B2 (filaments) and B4 (fluidic smear) are the two axes his stated taste has
  repeatedly landed on.
- **Archive dimensions (novelty distance): all of B1-B8.** Novelty can afford the
  extra dimensions; a grid cannot.

Proposed resolution: **6 bins per axis = 1296 cells.** Sized so that a
million-seed scan at ~0.9% pass rate (~9000 passers) can meaningfully populate
it without most cells sitting empty.

### Normalisation — must be FIXED, not data-driven

Each descriptor maps to [0,1] through a **constant, pre-committed transform**
(log-scale then clamp for the heavy-tailed ones: B1, B2). It must not be
normalised against the current run's own min/max — that would make cell identity
depend on what else happened to be scanned, breaking both reproducibility and
the ability to compare runs. Same law as the frozen enumeration contract.

---

## 6. Mechanism

1. **Archive** — `latent_behaviour.jsonl`, append-only, never reordered. One row
   per passer: id, the 8 descriptors, cell index, score. Same law as
   `latent_families.txt`.
2. **Novelty score** — mean distance to the *k*=15 nearest archive neighbours in
   normalised B1-B8 space (k=15 is the standard default in the literature).
3. **Pheromone deposits on NOVELTY, not on votes.** Grammar fragments that
   produced behaviourally-novel rolls get reinforced. This is a direct repoint of
   the existing `--swarm` plumbing (pheromone tables, manifest, LXS lineage) —
   the machinery survives, only the deposit rule changes.
4. **Emission = one elite per cell**, not top-K by score. This is what turns the
   output into a map instead of another ranked list.
5. **Votes act only within a cell**, deciding which roll holds it.

---

## 7. Risks, honestly

- **Descriptor choice is the entire ballgame.** Bad descriptors produce a
  meaningless map that still *looks* rigorous. This is the known hard part of QD
  and where the design attention belongs.
- **THE BIG ONE — the CPU field is not the rendered frame.** `SettleField`
  explicitly models feedback *without advection* ("fb' = fb*decay + inj*lens(f)",
  per-cell). The shader advects. So B2/B3/B7/B8 are measured on a **proxy** for
  what is drawn, and advection could smear apart distinctions the descriptors
  think are real. B1/B5 (from the curl field) are closer to ground truth.
  This is the assumption most likely to be false, and §8 is designed to break it
  early if it is.
- **Cells are not equally interesting.** A map can be perfectly diverse and
  mostly ugly. That is acceptable and expected — quality is Gary's layer — but it
  means like-rate is the WRONG success metric for this. See §9.
- **Genver coupling.** Descriptors are only comparable within one genver, exactly
  as queue.jsonl is. The archive must record the genver per row.

---

## 8. Validation experiment — run this BEFORE building anything

**Pre-registered, cheap, and capable of killing the idea in an hour.**

1. Compute B1-B8 for rolls that already exist (LX7 range, plus the existing
   graded library where genver permits).
2. Pick ~8 pairs: 4 pairs maximally DISTANT in map space, 4 pairs maximally CLOSE.
3. Render all 16 via `tools\render_check.ps1 -Grab`.
4. Show Gary the frames **unlabelled and shuffled**, and ask him to say which
   pairs look alike.

**Pass condition, committed in advance:** Gary sorts distant pairs as
"different" and close pairs as "alike" at clearly better than chance.

**If it fails**, the descriptors do not track perception and the honest move is
to fix them (or to move descriptors to the rendered frame, which costs a render
per candidate and changes the economics completely) — *before* any scan is spent.

---

## 9. Success metric — NOT like-rate

The three-haul experiment used like-rate, and that was right for a
quality-oriented method. It is the wrong metric here and using it would be a
category error.

Proposed, pre-committed:

- **Primary: behavioural coverage.** Cells occupied per 1M seeds scanned,
  QD-swarm vs cold as control. This is the direct measurement of the claim in §2.
- **Secondary: Gary's "same behaviours" verdict.** Show two unlabelled hauls and
  ask which one feels more varied. Blunt, but it is the actual thing being fixed.
- **Guardrail: like-rate must not COLLAPSE.** It is not expected to beat cold —
  a diverse map includes deliberately weird corners. If it craters, the map is
  diverse but worthless and the design needs rethinking.

---

## 10. Open decisions for Gary

1. **Map axes** — B1/B2/B3/B4 as proposed, or swap one for an audio axis
   (B5/B6)? Audio reactivity is central to what this app *is*, but it cannot be
   seen in a still frame, which makes it harder to validate in §8.
2. **Grid resolution** — 6 bins/axis (1296 cells) as proposed?
3. **Does the swarm emit the whole map, or the map's novel FRONTIER?** Emitting
   every filled cell each run means re-seeing filled cells; emitting only newly
   filled or newly improved cells is a smaller, fresher haul each time.
4. **Run §8 first, or design further first?**
