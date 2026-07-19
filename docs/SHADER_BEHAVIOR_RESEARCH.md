<!-- PROVENANCE: authored 2026-07-12 in the parent MilkdropProject as Gary's
     design spitballing plus public web/Shadertoy sources (list at bottom).
     Contains NO content derived from the closed MilkDrop 3.33 tree - the
     "engine constraints" it cites are ps_3_0 generalities plus THIS repo's
     own M2 buffer engine. Adopted into the clean tree 2026-07-18 as the
     behavior-family roadmap ("the vocabulary") for the open-scaffold
     direction in LATENT_GROWTH_DESIGN.md and for Bandit L2's layer slots. -->
# Shader Behavior Research — new technique families for the bandit

*2026-07-12. Companion to PALETTE_STRATEGIES_RESEARCH.md. Goal: find shader
technique families we do NOT already roll, that fit the engine and the taste,
so the bandit gets genuinely new behaviors (not recolors of old ones).*

## What we already have (don't re-buy)

Inks: cell, fractal (julia/KIFS), voronoi, march (SDF), neon, cloud (FBM),
cave (fold fractals), meta (metaballs), tunnel, starfield, plasma.
Comp: chrome (feedback-as-UV), color cycling, adaptive vibrance.
Medium: ONE family — curl-noise advection + decay + sharpen lace. This is the
biggest monoculture in the genome: every roll's "fluid" moves the same way.

## Engine constraints (what a candidate must survive)

- ps_3_0, legacy 2006 D3DX compiler (with modern-retry): no `break`, loops
  UNROLL — tap counts and loop trip counts are a real budget. `[loop]` helps
  on the modern retry only.
- Pixel shaders only. No compute, no vertex shaders, NO SCATTER WRITES —
  every technique must be expressible as "each pixel gathers".
- buf0..3 ping-pong feedback buffers, per-pass texsize, per-pass iters
  (chaining passes = multiple sim steps per frame). Blur chain blur1..6
  available as free pre-computed convolutions.
- CPU-side per-frame q-var sims (oscillators, three-body) for small-N state.

## The families, ranked

### Tier 1 — build next (high taste fit, low risk)

**1. Wave-equation water (ripple medium).**
State: height h + previous height h_prev in two channels of one buffer.
Update: `h_new = 2h - h_prev + c^2 * laplacian(h)` with mild damping — 5-9
taps, trivially gather-only. Shade in comp from the gradient (normals ->
specular sheen we already have) and audio injects: bass = dropped stones,
treble = rain, waveform ring = a moving wake. This is literally "music
disturbs an evolving medium". A NEW MEDIUM family, breaking the
curl-advection monoculture. Risk: near zero. The stencil is the same shape
as our existing sharpen lace.

**2. Real momentum fluid (Wyatt Flanders single-buffer).**
Famous Shadertoy result: acoustic-wave-equation + self-advection in ONE
buffer, no pressure solve. Per pixel: velocity.xy + two energy scalars;
update = backward advection `Field(p - v*dt)` plus neighbor exchange.
Produces persistent interacting vortices — actual momentum, where our curl
trick only fakes swirl per-frame. Alternative/extension: classic stable
fluids with a real pressure solve by CHAINING buf0->buf1->buf2 as Jacobi
iterations (we have per-pass iters — most Shadertoys don't). Risk: medium —
signed values in feedback buffers need checking (format), stability tuning.
Payoff: the single biggest "fluidic particle system" upgrade available.

**3. Gray-Scott reaction-diffusion as a first-class ink/medium.**
Two species U,V in two channels; laplacian + reaction, ~9 taps. The (F,k)
parameter plane IS a knob space: spots / stripes / coral / mitosis /
solitons are different regimes — the bandit rolls the regime. Audio
perturbs F/k or injects U on beats (patterns visibly reorganize to music).
Combos: RD advected by the medium = living tissue in a current. Our
"sharpen lace" was RD's little cousin; this is the real organism. Risk: low;
regimes near extinction boundaries can die — needs a reseed guard (we
already solved this class of bug with julia audio drift).

**4. Caustic/refraction comp mode (companion to 1 & 2).**
Treat the medium as a heightfield, refract a virtual light through it,
brighten by ray convergence (approx = divergence of the refracted gradient
— pure gather, cheap). Turns any wave/fluid roll into lit water. Comp-slot
addition like chrome, so it multiplies across all inks.

### Tier 2 — high payoff, moderate effort

**5. SmoothLife / Lenia (continuous cellular automata).**
Lifelike gliding, pulsing, self-organizing blobs — extremely on-taste
("you watch a system, not a graph"). Naive kernel = 100-200 taps (compiler
pain). THE TRICK: inner/outer disk averages ~= difference of two blur radii
— and the engine hands us blur1..6 FREE. SmoothLife via blur-difference is
a known approach; run it in a low-res buf (per-pass texsize) for stability.
Risk: medium — growth-function tuning is finicky, dead/exploded regimes
need the same reseed guard as RD. Knobs: birth/death thresholds, radius
ratio, timestep = whole families of creatures.

**6. Agent particles in a state row (boids / mini-physarum).**
Feasibility confirmed (Moroz survey): a small-N "loop over all particles"
system is gather-only and fine for HUNDREDS of agents. Layout: one row of
buf3 = agent states (pos, vel per pixel); update pass loops over all agents
(O(N^2), N=64-192); render = splat pass looping agents per pixel into a
low-res trail buffer that feeds the medium. Two personalities from one
mechanism: boids (cohesion/alignment/separation — ties to the Flocker
project) and physarum-style (sense blurred trail ahead, steer to gradient,
deposit). Trails advected by the medium = swarms swimming in fluid. This is
the first real step toward the particle dolphin. Risk: medium-high — loop
unrolling under the legacy compiler (verify modern-retry path takes it),
one-frame-latency state reads, splat loop budget (keep N and splat radius
modest or splat into quarter-res).

**7. Field-based pseudo-physarum (no agents).**
Gather-only approximation: advect the trail field along the gradient of its
own blurred self (+ rotation bias), deposit + decay. Produces branching
filament networks without any agent state. Cheap (blur chain again), rolls
well (bias angle / sensing radius / decay = knobs). Less "alive" than true
agents but nearly free. Good warm-up for 6.

### Tier 3 — interesting, park until asked

**8. Hydraulic erosion terrain (solid-world wing).**
Heightfield + rain + flow + sediment (virtual-pipes model is gather-able:
read neighbors' outflows). Lit like the gen-15 solid worlds. Slow, majestic
evolution; audio = rainfall intensity. Multiple coupled fields = our most
complex sim yet. Do after fluids prove the buffer plumbing.

**9. DLA / dendrite growth.** Pixel freezes when a neighbor is frozen (+
probability, + audio-gated rate); decay/re-melt for cycles. Gather-only,
easy, but slow to develop visually — better as a garnish ink than a lead.

**10. Voronoi particle tracking (the endgame).** Millions of particles,
gather-only: each pixel tracks its nearest particle's ID + state via
neighbor search. Confirmed ps_3_0-viable in principle (Moroz), but fiddly
(particle loss, velocity caps). This is the eventual particle-dolphin
substrate. Research spike LATER, after 6 works.

**Rejected: Chladni/cymatics plates.** Standing-wave mode patterns driven by
the spectrum — inherently symmetric mandala-ish output, and it's a direct
audio->pixel readout. Both on the user's dislike list.

## Genome mapping (where each lands in the bandit)

- MEDIUM slot (new axis! currently 1 family): wave-water (1), Wyatt fluid
  (2), pseudo-physarum drift (7). Roll medium family like we roll ink.
- INK slot: Gray-Scott (3), SmoothLife/Lenia (5), DLA (9).
- ACTOR/INJECTOR slot: agent row (6) — deposits into whatever medium rolled.
- COMP slot: caustics (4) — joins chrome/cycling as a rollable finisher.
- Cross-products are the payoff: RD ink x fluid medium x caustic comp is a
  behavior space none of the current 11 inks can reach.

## Sim-buffer budget note

Tier-1 items each want ONE dedicated buffer with 2-4 channels + iters.
Current layered composition uses buf0/1/2 (+ agent row would claim buf3).
Medium families REPLACE the current buf0 environment shader, not add to it,
so the budget fits — but a roll can't have wave-water AND Wyatt fluid AND
an agent swarm all at once. Dispatcher weights like the palette strategies.

## Sources

- Michael Moroz, "Overview of Shadertoy particle algorithms" —
  https://michaelmoroz.github.io/TODO/2021-3-13-Overview-of-Shadertoy-particle-algorithms/
- danilw, "Particle interaction on GPU shaders" —
  https://danilw.github.io/blog/Particle_interaction_on_GPU_shaders_particle_physics/
- Andy Green, "Playing With Runtime Fluid Simulation" (Wyatt Flanders port
  notes) — https://andytech.art/playing-with-runtime-fluid-simulation
- Wyatt Flanders on Shadertoy — https://www.shadertoy.com/user/wyatt
- Shadertoy references: expansive reaction-diffusion (4dcGW2), Multistep
  Fluid Simulation (MdSczK), Stateful particle system (lsy3R1).
- jasonwebb/morphogenesis-resources (technique catalog) —
  https://github.com/jasonwebb/morphogenesis-resources
