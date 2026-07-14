# M1b(2) — post-menu-build crash trace (RESOLVED to root-cause area)

**Goal:** standalone `vis_milk2.exe` runs the pristine MD2 plugin headless with a
synthetic waveform. It crashes *after* the menu is built during
`PluginInitialize`.

## VERDICT (this session)
The crash is **inside `CPluginShell::AllocateDX9Stuff()`** — the step right after
the menu build — while loading/compiling the **first preset's NSEEL code**.
It is a **`STATUS_BREAKPOINT` (0x80000003)** — an `int 3` (0xCC) — executing at a
**heap/VirtualAlloc address with no owning module** (`mod=?`), and the address
**changes every run** (`0x0D54ED75`, then `0x0FA6FBE2`). That signature = we are
running **NSEEL2 JIT-generated machine code** and hitting a breakpoint byte in it.
This is the classic MD2 port bug: NSEEL's hand-rolled x86 asm code-stubs get
mis-sized/mis-copied under a modern optimizer, so generated code is corrupt.

## Evidence trail (host_trace.log)
```
start WinMain
before PluginPreInitialize
after PluginPreInitialize; before PluginInitialize
PI: before InitDirectX
PI: before InitNondx9Stuff      <- menu is built in here (InitGDIStuff), SUCCEEDS
PI: before AllocateDX9Stuff     <- last breadcrumb before fault
VEH code=0x80000003 addr=0FA6FBE2 mod=? off=0x0
```
Init order (pluginshell.cpp PluginInitialize ~L1322):
InitDirectX → InitNondx9Stuff(→InitGDIStuff builds menu) → **AllocateDX9Stuff** → InitVJStuff.
No modal dialog (`host_dialogs.log` empty) and NDEBUG is set (asserts compiled out),
so it is NOT an assert/error-box path — it is corrupt generated code.

## Facts / environment
- Repo: `C:\Users\grben\MilkdropProject\MD2-port` (NOT under Claude Sessions cwd).
- Build: MSBuild v143 override (asserts off via NDEBUG). Command that works:
  ```
  & "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" `
    "C:\Users\grben\MilkdropProject\MD2-port\vis_milk2\milkdrop_standalone.vcxproj" `
    /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v143 `
    /p:WindowsTargetPlatformVersion=10.0.26100.0 /m /v:m /nologo
  ```
  (Default toolset in the .vcxproj is v100 and fails MSB8020; must override to v143.)
- Run: exe + cwd must be `...\vis_milk2\Release` (finds data\ shaders, milk2.ini).
- NSEEL_REENTRANT_EXECUTION is already defined in the Release preprocessor defs.

## Instrumentation left in the tree (TEMPORARY — remove before commit)
- `standalone_host.cpp`: `hostlog()`, `CrashVEH` vectored handler (logs
  code/addr/module/offset to `host_trace.log`), breadcrumbs around PreInit/Init,
  and `remove("host_trace.log")`+`AddVectoredExceptionHandler` at WinMain top.
- `pluginshell.cpp` PluginInitialize (~L1322): temporary `HTRACE(...)` macro +
  4 breadcrumb lines around the sub-steps. **Delete this block when done.**

## NEXT STEPS (fresh session starts here)
1. Focus on NSEEL2 codegen: `MD2-port/ns-eel2/`. The bug is that x86 asm stub
   lengths are computed from function-address deltas; modern MSVC (v143) reorders/
   COMDAT-folds functions so a stub's measured length is wrong → copied blob is
   garbage / includes 0xCC → int3 when executed.
   - Verify `nseel-caltab`/`asm-nseel-x86-msvc` (or equivalent) stub table.
   - Fix options, cheapest first:
     a) Build the ns-eel2 codegen TU(s) with `/Od` and disable optimization that
        reorders funcs; disable ICF for that lib (`/OPT:NOICF`), enable
        `/Gy-`? Actually need funcs NOT folded — try `#pragma optimize("",off)`
        around the asm-length marker functions, or put each stub in its own TU.
     b) Confirm the "function boundary" sentinel bytes NSEEL scans for are intact.
2. To pin the exact EEL site: in `AllocateDX9Stuff` (plugin.cpp) breadcrumb around
   the first `NSEEL_code_compile` / `NSEEL_code_execute` (per-frame then per-pixel).
   Expect the fault on first *execute*, not compile.
3. After fix: rebuild, rerun, expect trace to reach `PI: all sub-steps OK` and the
   window to render the synthetic-waveform preset. Then STRIP all instrumentation
   and commit M1b(2).

## ROOT CAUSE (found + FIXED)
Not COMDAT folding. `GLUE_realAddress` (nseel-compiler.c:273) for MSVC ignores the
`_end` symbol and scans forward from `fn` for a 12-byte sentinel (`0x89` + 11x`0x90`).
Its `0xE9` jump-thunk unwrap (line 280) is guarded by `#if defined(_DEBUG)`, so in a
**Release build with incremental linking**, `fn` is an ILT `E9 jmp` thunk, the scan
starts at the wrong place, every JIT asm stub is mis-measured -> corrupt generated
code -> `int3` on first EEL execute.

The standalone .vcxproj never set `<LinkIncremental>` (defaults true; `.ilk` was
present), whereas the known-good MD3 engine build sets it false.

**Fix applied:** `milkdrop_standalone.vcxproj` PropertyGroup — added
`<LinkIncremental>false</LinkIncremental>` for Release|Win32 and Debug|Win32.
(Pristine ns-eel2 code left untouched — the fix is purely a build setting.)

Result: init runs to `PI: all sub-steps OK` -> render loop; window renders a live
fluid caustic preset (captured to `M1b2_window.png`). Milestone visual verified.

## REMAINING (non-fatal) — for next session
Per-frame first-chance `0xC0000005` at `KERNELBASE.dll+0x108AC7`, logged repeatedly
by our VEH but swallowed by the plugin's own `__try/__except` (process never exits,
render is correct). Likely a guarded null deref in the per-frame path. Decide if it
needs fixing or is benign SEH noise; our first-chance VEH is what makes it visible.

## CLEANUP BEFORE COMMIT (instrumentation still in tree)
- `standalone_host.cpp`: remove `hostlog`, `CrashVEH`, `AddVectoredExceptionHandler`,
  `remove("host_trace.log")`, and the PreInit/Init breadcrumb lines.
- `pluginshell.cpp` PluginInitialize (~L1322): remove the `HTRACE` macro + 5 breadcrumbs.
- KEEP the `.vcxproj` LinkIncremental fix (that's the real change).
- Then rebuild once clean, confirm still renders, commit M1b(2).

## PIVOT (new direction from Gary) — top menu bar like MilkDrop 3.33
Gary showed 3.33: it has a real **top MENU BAR** (File · Effects · Sprites ·
Playlist · Audio · Options · Help · About). Classic MD2 never had one (Winamp-
embedded, right-click only). New sub-milestone: **add a native menu bar to the
standalone host.** He also sees "no border between window and background" on his
screen (OS reports WS_OVERLAPPEDWINDOW frame IS present — reconcile with his own
full-screen shot; may be off-top positioning). Wants F1-help content surfaced into
menus. NOT copying effects/functionality — just the menu "equipment" for features
we actually port.

### DECISION MADE: WHITE-ROOM ONLY (hard constraint)
Gary: "we are trying to be 100% legitimate, we can only whiteroom it." AND the key
fact — the MilkDrop3 GitHub repo has a BSD LICENSE.txt but the **real 3.33 source is
NOT published** (the shipping exe's code isn't in the tree). So there is no
legitimately-available source to port regardless; reconstruct from observed behavior.
- HARD RULE: **never open any MilkDrop3 source** for the menu (esp.
  `MilkDrop3-main/.../milkdrop3_menu.cpp`, `Milkdrop2PcmVisualizer.cpp`). Reading it
  taints the clean-room. The **3.33 screenshot is the ONLY spec.**
- Observable spec from Gary's 3.33 screenshot (title bar "MilkDrop 3.33  Press F1
  for help"), top menu bar, left→right:
  3.33 shows: File · Effects · Sprites · Playlist · Audio · Options · Help · About
  OUR bar (About folded into Help per Windows norm):
  **File · Effects · Sprites · Playlist · Audio · Options · Help**
- Only implement drop-down items for features we are actually porting; surface the
  floating F1-help content into relevant menus. Not copying his effects/functionality.

### White-room menu plan (next window)
1. FIRST cement baseline: strip instrumentation (see CLEANUP list) + `git commit`.
2. In the standalone host (or dxcontext hwnd owner), build a menu bar from scratch:
   `CreateMenu()` + `CreatePopupMenu()`/`AppendMenuW()` for the 8 top items, then
   `SetMenu(hwnd, hMenuBar)`. Assign our own WM_COMMAND ids (do NOT reuse MD's
   resource.h ids blindly — define fresh in the host).
3. A menu bar consumes client height → the D3D backbuffer/client shrinks. Verify
   dxcontext client-rect math (AdjustWindowRect currently uses MY_WINDOW_STYLE with
   no menu) and that resize/StartOrRestartDevice still works with a menu present.
4. Route WM_COMMAND in CPluginShell::WindowProc (or a host subclass) to real actions
   only for ported features; leave the rest greyed/stubbed.
5. Reconcile Gary's "no border" observation while here (OS reports WS_OVERLAPPEDWINDOW
   frame present; confirm against his own full-screen shot — may be off-top position).

### Observed dropdown contents (white-room spec from Gary's 3.33 screenshots)
Implement ONLY from these observations — never MD3 source.
- **File**: New · Open · Add to Favorites · Exit
- **Effects**: SKIP for now (Gary: leave out)
- **Sprites**: SKIP for now (Gary: leave out)
- **Playlist**: Load playlist · Stop playlist (greyed when none running).
  NOTE: loads *preset* playlists (lists of .milk presets), not audio.
- **Audio**: Auto (radio-checked ✓) · Speakers ▸ · Microphones ▸
  (Speakers/Microphones are device-list submenus → this is the M1c WASAPI
  capture-source picker; "Auto" = default loopback.)
- **Options**: per-preset settings; **session-only unless "Save settings" pressed**.
  Adopt that save behavior, BUT Options is BLOCKED on building the aggregate preset
  dials (not created yet) → defer Options until that feature exists.
- **Help** (top-level; standard Windows convention = Help holds About, NOT vice
  versa). Layout:
    Help
     ├─ View Docs
     ├─ ────────
     └─ About MilkDrop…   (dialog)
  SKIP "Watch on YouTube" for now. NO Donate link. NO paid Upgrade (none for now).
  About dialog can be a simple placeholder; flesh out later.
- (3.33 had a separate top-level "About" with Donate/Upgrade/About — we do NOT copy
  that; fold the single About item into Help per the norm.)

### Overlays (F1 help, preset-name)
The overlay ENGINE is already in our pristine MD2 base (pluginshell: `m_show_help`,
`ID_SHOWHELP`, preset-name overlay via WASABI_API_LNGSTRINGW) — i.e. part of the
"equipment we're moving," not something to white-room from scratch. Gary's *modified*
3.33 overlays are separate; he'll load his build so we can white-room those specifics
from a screenshot. Our HostLanguageStub currently returns EMPTY strings, so overlay
text is blank until we feed real strings (tie-in for the menu/help work).
Gary also: "we built our own menu item for my features, we may rip those out" —
any pre-existing custom menu items (from our buffer/latent-search work) are
candidates for removal; confirm before wiring.

### Window-mode behavior spec (from Gary, observed 3.33 behavior)
Three states, and the menu bar is tied to mode:
- **Windowed** (normal resizable, WS_OVERLAPPEDWINDOW): menu bar SHOWN.
- **Maximized** (still framed window): menu bar SHOWN.
- **Fullscreen**: NO borders, NO menu bar (borderless WS_POPUP).
=> Attach `SetMenu(hwnd, bar)` when entering WINDOWED/maximized; `SetMenu(hwnd, NULL)`
   when entering FULLSCREEN/FAKE_FULLSCREEN. dxcontext already maps WINDOWED→
   WS_OVERLAPPEDWINDOW and FULLSCREEN/FAKE_FULLSCREEN→WS_POPUP (dxcontext.cpp:44),
   so hook menu add/remove into the mode-switch / StartOrRestartDevice path and
   recompute client rect (menu height only applies in windowed/maximized).

## Checkpoints
- [x] SP1: reproduced (0x80000003, module-less heap addr, varies per run).
- [x] SP2: located — fault is in AllocateDX9Stuff, post-menu.
- [x] SP3: root-cause area = NSEEL JIT generated-code breakpoint.
- [x] SP4: FIXED via LinkIncremental=false; rebuilt; window renders (M1b2_window.png).
- [x] SP5: instrumentation stripped, clean rebuild, committed M1b(2) baseline (9f0b3ca).
- [ ] SP6 (deferred): investigate the per-frame caught AV (KERNELBASE +0x108AC7).

## MENU BAR — DONE (this session)
Built the white-room top menu bar for the standalone host. Verified: window shows
title bar + frame + bar **File · Effects · Sprites · Playlist · Audio · Options ·
Help** (M1b2_menu.png); WM_COMMAND routing works end-to-end (View Docs toggled the
F1 help overlay, M1b2_help.png).

Design (all standalone menu code gated behind `MILKDROP_STANDALONE`, defined only in
milkdrop_standalone.vcxproj → the Winamp DLL build stays byte-for-byte pristine):
- `host_bridge.h` + `standalone_host.cpp`: the menu is host-owned "equipment".
  `HostBuildMenuBar()` builds it; `HostHandleMenuCommand()` handles our private-range
  ids (0xE1xx, no collision with ID_VIS_* / resource.h). Actions use only public
  engine methods + the hwnd. Live items: File>New (LoadRandomPreset), File>Open
  (GetOpenFileName→LoadPreset), File>Exit, Audio>Auto(checked), Help>View Docs
  (ToggleHelp), Help>About (placeholder MessageBox). Greyed placeholders: Effects,
  Sprites, Options (deferred), Add-to-Favorites, Playlist>Stop, Audio device submenus.
- `dxcontext.cpp`: attach the menu on windowed-window creation (keyed on
  screenmode==WINDOWED, so mode switches add/remove it automatically); two
  AdjustWindowRect calls made menu-aware so the client == D3D backbuffer.
- `plugin.cpp` MyWindowProc WM_COMMAND: one gated line forwards to the host handler
  first; unknown ids fall through to the engine unchanged.
- Dialog watchdog now disarms after init (g_suppressDialogs=false) so menu-driven
  Open/About boxes aren't auto-dismissed.

### "No border" reconciled (Gary's observation) — root cause + fix
Classic MD2's WndProc returns 0 for **WM_NCCALCSIZE in every mode**, collapsing the
client to fill the whole window — which is exactly why there was no visible frame AND
why an attached menu bar had zero height. Fixed in pluginshell.cpp: in standalone
WINDOWED mode, let the OS compute the normal non-client area (DefWindowProc);
fullscreen/desktop (WS_POPUP) keep the collapse. Now client is a clean 512x512 with a
real title bar + menu; nonclient height 59px.

### OPEN for Gary
- Effects/Sprites REMOVED (Gary: 3.33-only, not porting; no placeholders — add new
  categories later when the features exist). Bar is now File·Playlist·Audio·Options·Help.
- **Options** kept as the one greyed item (it IS on our roadmap: per-preset dials +
  save behavior). Remove it too by the same principle, or leave until built? (pending)
- Separate pre-existing bug (NOT menu-related): some presets fail shader compile with
  `X3004: undeclared identifier 'sampler_buf1'` (from the multi-pass buffer work);
  shows as the red error overlay. Track/fix separately.
- M1c hooks left in place: Audio>Speakers/Microphones submenus + Playlist load are the
  WASAPI-capture / preset-playlist tie-ins.
