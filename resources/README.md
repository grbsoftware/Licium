# resources/

Tracked copies of runtime data files that the gitignored `vis_milk2/Release/`
staging tree needs. When these change, re-copy them into
`vis_milk2/Release/Milkdrop2/data/` (a .fx edit needs no engine rebuild, just
an app restart).

- `Milkdrop2/data/include.fx` — the genuine 2013 MD2 shader preamble (BSD,
  obtained via the xbmc/visualization.milkdrop2 packaging) **plus Licium's
  multi-pass buffer declarations** (`sampler_buf0..3`, `texsize_buf0..3`,
  `GetBuf0..3`). The other `data/*.fx` files in the staging tree are unmodified
  MD2 stock; full packaging of the runtime tree is an M5 concern.
- `Milkdrop2/presets/_test_buf_fb.milk` — buffer-engine regression preset:
  buf0 accumulates a decaying trail behind a moving blob, comp displays buf0.
  Correct = an orange comet with a fading tail; a lone dot = feedback broken;
  red overlay = compile broken.
