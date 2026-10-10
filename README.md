<p align="center">
  <img src="brand/licium_icon_128.png" alt="Licium" width="96" height="96">
</p>

# Licium

**A taste-guided latent-shader explorer.** Licium generates music-reactive
shader visuals nobody designed, learns what you like, and goes looking for
more of it. It is a standalone Windows app built on a clean-room port of the
BSD-licensed MilkDrop 2 rendering engine.

> **Shelved at v0.2.1, July 2026.** This is a working record of how far it
> got, published so the work and the method are readable. It builds and runs;
> it is not a product, has no installer, and is not under active development.

*Logo and visual identity by **Holly Watson**.*

## What it actually did

The interesting part is not the renderer — it is that the search ran, at
scale, and was graded.

| | |
|---|---|
| Shader presets generated and machine-verdicted | **1,492** |
| Judged alive by the automated eye | **1,031** (69%) |
| Blowout / late-blooming / frozen / failed to load | 235 · 135 · 66 · 25 |
| Capture failures across the whole corpus | **0** |
| Generation versions shipped, all still reproducible | **10** (LX1 → LX10) |
| Rolls graded by a human | 447, of which 108 liked |

Every one of those presets is regenerable from its ID alone. The short code
stamped in the corner *is* the seed — the same ID rebuilds the same preset,
byte for byte, under the generation version that made it. Nothing is ever
deleted: every roll is a file, every screened candidate stays in a ranked
queue, and rejected rolls live on a browsable glitch shelf.

<p align="center">
  <img src="docs/gallery/LXS4-01B7A8B4.jpg" width="32%">
  <img src="docs/gallery/LX1-00004435.jpg" width="32%">
  <img src="docs/gallery/LX7-020D4F2F.jpg" width="32%">
</p>

**[→ Gallery](docs/gallery.md)** — twenty-five rolls from the liked set, each
with the ID that regenerates it.

## The three pillars

- **Bandit (`B`)** — a one-armed bandit for shader space. Every press rolls
  a brand-new feedback-fluid preset from a seeded generator.
- **Palettes (`V`)** — a voting wall of generated color palettes. Likes feed
  a taste pool; the Bandit dresses most of its rolls from palettes you
  actually liked.
- **Latent (`X`)** — an automated prospector that grows shader formulas as
  chains through a typed grammar (so everything compiles by construction),
  screens millions of candidates on the CPU, triages survivors in the real
  renderer, and presents them in-app to vote, tag, and describe. Your
  verdicts steer later scans.

## Engine features added to the MD2 base

- **Multi-pass buffer engine** — four generic ping-pong feedback buffers
  (`bufN_` shader blocks, `sampler_buf0..3`, per-pass iteration counts,
  mixed resolutions), the substrate the generators build fluid media on.
  The ping-pong flips *inside* the frame, so a pass can relax over its own
  output — enough for solver-style passes, not just decay feedback.
- **Shaping-curve LUTs** — four 256×1 half-float tables (`sampler_curve0..3`)
  baked per-preset from a seed. One texture fetch buys an arbitrary response
  curve at flat instruction cost, which is what keeps wild rolls inside the
  ps_3_0 budget.
- **A modern shader compiler path** — `D3DCompile` from `d3dcompiler_47.dll`
  gets first refusal, with D3DX9 as fallback. The legacy compiler does not
  reject deeply-nested generated expressions, it *crashes the process* on
  them.
- **Reworked audio envelope** — the stock per-frame lerp is replaced with an
  asymmetric damped spring: underdamped attack so levels bounce, critically
  damped release so they coast instead of snapping to dark. No preset
  semantics change; it is the envelope every `*_att` value was always read
  through.
- **Standalone host** — no Winamp required: WASAPI loopback/microphone
  audio, a real menu bar, playlist folder auto-advance, window-state
  persistence, and a Spout sender publishing every frame for external
  capture.

## Building

Requires Visual Studio 2022 Build Tools (v143 toolset) and the DirectX SDK
(June 2010) staged at `DirectX\Include` + `DirectX\Lib` (not committed).

```powershell
& "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" `
  vis_milk2\milkdrop_standalone.vcxproj `
  /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v143 `
  /p:WindowsTargetPlatformVersion=10.0.26100.0 /m
```

Output: `vis_milk2\Release\Licium.exe`. Runtime data (shaders, presets,
settings) lives beside the exe under `Licium\`; tracked copies are in
`resources\`. The original Winamp plugin still builds from
`vis_milk2\milkdrop.vcxproj` and stays byte-for-byte free of Licium code
(all Licium features are `MILKDROP_STANDALONE`-gated).

## Design docs

Eleven documents in [`docs/`](docs/README.md), written as the work happened.
They record decisions that were reversed and tests that failed, not a tidied
account — [docs/README.md](docs/README.md) says where each one actually
landed, since several status headers were true when written and are not now.

## Provenance

Every line of Licium traces to this repository and its design docs
(`docs/*.md`). The engine base is Ryan Geiss's final public MilkDrop 2
source drop (May 2013, BSD 3-Clause, Copyright 2005-2013 Nullsoft, Inc.).
Licium's own features — the buffer engine, the three pillars, the
standalone host — were designed and written fresh against that base; no
closed-source MilkDrop derivative was consulted. Tool output formats
(design docs, ID lineages, vote files) are documented in `docs/`.

**Naming is deliberately two-layer.** Everything a user sees carries the
product name — the window and its caption, the icon, the `Licium\` data
folder, `licium.ini`. The engine sources keep their original MilkDrop names,
so the tree stays diffable against Geiss's drop: 1,259 lines added into the
pristine engine files, 27 removed, every Licium feature behind a single
`MILKDROP_STANDALONE` gate. That diff is what makes the clean-room claim
checkable rather than merely asserted, and it is worth more than a
cosmetically uniform namespace. The About box names the engine and its
license for the same reason.

## Credits

- **Holly Watson** — Licium logo, splash wordmark, visual identity
  (`brand/CREDITS.md`).
- **Ryan Geiss / Nullsoft** — MilkDrop 2 engine (BSD 3-Clause).
- **Lynn Jarvis** — Spout (BSD 2-Clause).
- Curated palette data regenerated from the MIT-licensed
  *nice-color-palettes* and *dictionary-of-colour-combinations* projects.

## License

BSD 3-Clause — see [LICENSE](LICENSE). Vendored components keep their own
compatible licenses, preserved in place.
