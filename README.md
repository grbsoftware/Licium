# Licium

**A taste-guided latent-shader explorer.** Licium generates music-reactive
shader visuals nobody designed, learns what you like, and goes looking for
more of it. It is a standalone Windows app built on a clean-room port of the
BSD-licensed MilkDrop 2 rendering engine.

*Logo and visual identity by **Holly Watson**.*

## The three pillars

- **Bandit (`B`)** — a one-armed bandit for shader space. Every press rolls
  a brand-new feedback-fluid preset from a seeded generator. The short ID
  stamped in the corner *is* the seed: the same ID regenerates the same
  preset, byte for byte, forever.
- **Palettes (`V`)** — a voting wall of generated color palettes. Likes feed
  a taste pool; the Bandit dresses most of its rolls from palettes you
  actually liked.
- **Latent (`X`)** — an automated prospector that grows shader formulas as
  chains through a typed grammar (so everything compiles by construction),
  screens millions of candidates on the CPU, triages survivors in the real
  renderer, and presents them in-app to vote, tag, and describe. Your
  verdicts steer later scans.

Nothing is ever deleted: every roll is a file, every screened candidate
stays in a ranked queue, and rejected rolls live on a browsable glitch
shelf.

## Engine features added to the MD2 base

- **Multi-pass buffer engine** — four generic ping-pong feedback buffers
  (`bufN_` shader blocks, `sampler_buf0..3`, per-pass iteration counts,
  mixed resolutions), the substrate the generators build fluid media on.
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

## Provenance

Every line of Licium traces to this repository and its design docs
(`docs\*.md`). The engine base is Ryan Geiss's final public MilkDrop 2
source drop (May 2013, BSD 3-Clause, Copyright 2005-2013 Nullsoft, Inc.).
Licium's own features — the buffer engine, the three pillars, the
standalone host — were designed and written fresh against that base; no
closed-source MilkDrop derivative was consulted. Tool output formats
(design docs, ID lineages, vote files) are documented in `docs\`.

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
