# Licium hotkey registry

**Rule: before assigning ANY new hotkey (Bandit, Palettes, Latent, future
tools), check this table AND the handlers it is derived from** —
`plugin.cpp` `HandleRegularKey()` (WM_CHAR keys), `plugin.cpp` `case
WM_KEYDOWN` (virtual keys), and `pluginshell.cpp` `PluginShellWindowProc`
(keys intercepted before the plugin sees them). Update this file in the same
commit that binds or frees a key. The F1 overlay text
(`standalone_host.cpp`, `LICIUM_HELP_TEXT`) documents the user-facing subset —
keep it in sync.

Engine keys are context-sensitive: while a text prompt ("waitstring"), the
load menu (L), the settings menu (M), or mash-up mode is active, most
printable keys are consumed by that mode first. Global tool hotkeys must
therefore expect to be shadowed inside those modes — or use key chords the
modes never consume (Ctrl+letter is mostly free, see below).

## Occupied — WM_CHAR (case-sensitive where noted)

| Key | Action | Notes |
|-----|--------|-------|
| `0`–`9` | numeric input (sprites/messages) | 2-digit accumulator |
| `q` / `Q` | video echo zoom − / + | |
| `w` / `W` | waveform mode next / prev | |
| `e` / `E` | wave alpha − / + | |
| `i` / `I` | zoom + / − | |
| `o` / `O` | warp amount − / + | |
| `t` / `T` | song-title anim | needs track title; inert standalone |
| `u` / `U` | Winamp shuffle toggle | Winamp IPC — inert standalone |
| `r` | random ↔ sequential preset order | |
| `a` / `A` | random preset + random shaders | |
| `d` / `D` | cycle shader locks | |
| `f` / `F` | video echo orientation | |
| `g` / `G` | gamma − / + | |
| `h` / `H` | hard cut (mash-up: re-roll slot/all) | |
| `j` / `J` | wave scale − / + | |
| `k` / `K` | sprite / sprite-kill numeric mode | |
| `l` / `L` | load-preset menu | |
| `m` / `M` | engine settings menu | |
| `n` / `N` | debug info overlay | |
| `s` / `S` | save preset as… | |
| `y` / `Y` | custom-message numeric mode | via keyMappings |
| `!` / `@` | randomize warp / comp shader | |
| `[` `]` `{` `}` | push x/y | |
| `<` `>` | rotation | |
| `-` `+` | preset rating | |
| `*` | clear numeric input | |

## Occupied — virtual keys (WM_KEYDOWN / pluginshell)

| Key | Action | Notes |
|-----|--------|-------|
| `F1` | help overlay | pluginshell |
| `F2`–`F6` | title / time / preset info / fps / rating overlays | |
| `F7` | re-read custom messages | |
| `F8` | change preset folder | |
| `F9` | shader help (in editor) | |
| `Esc` | close help/menu/fullscreen | pluginshell |
| `Alt+Enter` | fullscreen toggle | pluginshell |
| `Space` | soft cut | |
| `Scroll Lock` | preset lock | |
| `↑ ↓` | volume (Winamp IPC — inert standalone) | pluginshell |
| `← →`, `PgUp/PgDn`, `Home/End`, `Enter`, `Backspace`, `Del`, `Ins` | menu/list navigation; Backspace = prev preset in regular mode | mode-dependent |

## Occupied — Licium tool keys (host-first, `HostHandleChar` in `standalone_host.cpp`)

These are checked before the engine's own hotkeys, but only in the regular UI
mode — the load menu (`L`), settings menu (`M`), mash-up and text prompts own
the printable keys while they are up, so a tool key is shadowed there.

| Key | Action | Notes |
|-----|--------|-------|
| `b` / `B` | Bandit: roll a new preset | ID `L1-XXXX` = the seed; also on the Bandit menu |
| `v` / `V` | Palettes: open the voting wall | modal while open — the wall eats ALL keys: arrows move, `Y`/`Space` like, `N`/`X` pass, `U` clear, `PgUp/PgDn` page, `V`/`Esc` close |

## Free for Licium tools

- **Letters (global WM_CHAR):** `c` `p` `x` `z` (upper+lower).
  Suggested reservations: `x` Latent e**x**plore (`l` is taken by the load
  menu). (`b` and `v` are now taken — Bandit and the Palettes wall, above;
  `p` freed up since Palettes took `v`.) (`b` is now taken — Bandit, below.)
- **Function keys:** `F10`–`F12` (F10 activates the menu bar by Windows
  convention — prefer F11/F12).
- **Ctrl chords:** the engine binds no Ctrl+letter combos in the standalone
  paths (Winamp-forwarding aside); a Ctrl+ prefix is the safest namespace for
  tool options.
- **Punctuation:** `` ` `` `~` `,` `.` `/` `;` `'` `\` `=` `_` `#` `$` `%`
  `^` `&` `(` `)` `?` `"` `:`

## Standalone-inert engine keys (do NOT rebind yet)

`t`/`T`, `u`/`U`, `↑`/`↓` reach Winamp-IPC code that no-ops without Winamp.
They still LOOK free in the standalone build, but the pristine handlers
consume them — rebinding would need an engine-side gate. Leave them alone
until a deliberate decision.
