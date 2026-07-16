# Credits

## Design / brand

**Licium logo and visual identity — Holly Watson.**

The milk-splash wordmark is Holly Watson's design. The application icon (black "L"
with droplet, `licium_icon.svg` / `licium.ico`) is derived from that identity.

Please keep this attribution with the artwork, and carry it into the application's
About box and any distributed build.

## Engine

Licium's rendering engine is a **clean-room port of MilkDrop 2**:

> Copyright 2005-2013 Nullsoft, Inc. — BSD 3-clause.
> Originally written by **Ryan Geiss**.

MilkDrop 2 is the engine's ancestry only; Licium's direction (taste-guided
exploration of a latent shader space) is its own. The BSD notice and the
Nullsoft / Ryan Geiss attribution must be preserved in source and in any
distributed build.

## Third party

- **Spout** (DirectX 9 texture sharing, `vis_milk2/spout/`) — Copyright (c)
  2020-2024 Lynn Jarvis. BSD; see `vis_milk2/spout/LICENSE`.
- **ns-eel2** — expression evaluator shipped with MilkDrop 2 (Nullsoft).
- **Winamp SDK headers** — permissive (zlib-style); retained for the plugin build.

## Assets

| File | What |
| --- | --- |
| `licium_icon.svg` | Icon source (256x256, black L + droplet on white) |
| `licium.ico` | Multi-size app icon: 256, 128, 48, 32, 16 |
| `licium_icon_*.png` | Per-size PNG renders |
| `licium_splash.png` | Holly Watson's milk-splash wordmark (1254x1254), shown as the startup splash (embedded via `licium.rc`) |

Note: the droplet is carried at 256/128/48/32 and omitted at 16, where it would
render as a ~1px speck. The original of the splash wordmark lives in
`MilkdropProject\brand\` ("Licium brand logo.png"); this repo copy is the one
that ships.
