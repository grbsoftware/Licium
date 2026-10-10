# Design docs — what each one is, and where it actually got to

These eleven documents are a working record, not a specification. They were
written as the work happened, and several carry status headers that were true
when written and are not true now — a doc saying "awaiting blessing, no code
until then" may describe something that shipped a week later, or something
that was tested and failed.

Rather than rewrite those headers after the fact, this index states where each
one actually landed. **Statuses verified against the code on 2026-10-09**; the
last engine commit was 2026-07-28.

| Doc | What it is | Where it landed |
|---|---|---|
| [BANDIT_DESIGN.md](BANDIT_DESIGN.md) | The seeded preset generator (`B`) | **Shipped** as specified. `BANDIT_GENVER 1`. |
| [PALETTES_DESIGN.md](PALETTES_DESIGN.md) | The palette voting wall (`V`) | **Shipped** as specified. `PALETTE_GENVER 1`. |
| [LATENT_DESIGN.md](LATENT_DESIGN.md) | The grammar prospector (`X`), v1 | **Shipped**, then carried well past this spec — ten generation versions, LX1 through LX10. |
| [SHADER_CURVES_DESIGN.md](SHADER_CURVES_DESIGN.md) | Shaping-curve LUTs and the families search | **Shipped through step 4.** Steps 1–3 landed as LX5, families as the `LXF` trunk. Step 5 (generalising the LUT) deferred. |
| [LATENT_GROWTH_DESIGN.md](LATENT_GROWTH_DESIGN.md) | Cold prospecting, the stub pile, the Bandit promotion gate | **Partly built.** Swarm mode and the open scaffold shipped (LX4 onward). The subtree miner, the promotion gate's share caps, and GPU screening were designed and never built. |
| [TUNE_COCKPIT_DESIGN.md](TUNE_COCKPIT_DESIGN.md) | A docked tool strip for tuning a selected roll | **Partly built.** The dock shipped with ten slots; two are wired (Color, Search). The other eight report themselves unbuilt rather than pretending. |
| [SWARM_DIVERSITY_DESIGN.md](SWARM_DIVERSITY_DESIGN.md) | Giving the swarm diversity as its objective instead of taste | **Tested and failed.** The doc pre-registers a test, then records that its first descriptor set failed it 4 of 8. The negative result is the finding. |
| [BANDIT_L2_DESIGN.md](BANDIT_L2_DESIGN.md) | Layers, the spiral family, remix | **Never built.** `BANDIT_GENVER` is still 1. |
| [AUDIO_TEXTURE_DESIGN.md](AUDIO_TEXTURE_DESIGN.md) | Feeding audio history to shaders as a texture | **Never built.** Captured so the reasoning survived the conversation. |
| [SHADER_BEHAVIOR_RESEARCH.md](SHADER_BEHAVIOR_RESEARCH.md) | Candidate behaviour families — wave water, reaction-diffusion, caustics | **Reference.** None of the families were implemented; it is the roadmap the growth doc points at. |
| [HOTKEYS.md](HOTKEYS.md) | Key registry for the whole app | **Current.** Operational reference, not a design doc. |

## Reading them

Three are worth the time even if you skip the rest:

- **LATENT_GROWTH_DESIGN.md** for the moment the project's premise got
  rejected — the search was exploring where ink appears while every property
  a viewer actually judges sat outside it.
- **SWARM_DIVERSITY_DESIGN.md** for a pre-registered test that failed and was
  recorded as failed.
- **m1b2/M1b2_crash_trace.md** for a `STATUS_BREAKPOINT` at an address with no
  owning module, moving every run, tracked down to NSEEL's hand-rolled x86
  stubs being mangled by a modern optimizer.

Decisions reversed by evidence are kept, not deleted. LX9 shipped an exposure
floor, had thirteen of its fourteen affected rolls rejected on sight, and was
narrowed to a one-roll rescue in LX10 — all three versions still regenerate
their own IDs.
