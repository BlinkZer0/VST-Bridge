# Phase 4 report: plugin editors (GUIs)

_2026-11-18. Device: Galaxy S26 Ultra (SM-S948U1), Android 17. Both editions tested._

## Result

A bridged plugin's own Windows editor opens on the phone, draws correctly (fonts included), and
takes touch input as mouse input. It runs on the **same plugin instance that is playing**, and audio
keeps playing with no underruns while the editor is used.

Tested with **Odin2** (the only one of the four test plugins with a classic Win32/OpenGL editor; the
other three use WebView2 editors, which stay blank under Wine and are covered by Phase 5).

## How it works

```
EditorActivity (Kotlin)  ── editor_open ──▶  vstghost-host.exe serve (Wine/Box64)
     ▲  bitmap  │ touch                         │ IPlugView in a Win32 window
     │          ▼                               ▼
  vgwl compositor (shared/wl, in the app) ◀── Wine's Wayland driver (winewayland.drv)
     Unix socket rootfs/run/wl-0, shm buffers, wl_pointer events
```

- **Compositor (`shared/wl/vg_wl.cpp`)**: a minimal Wayland server written directly against the
  wire protocol, so no libwayland or libffi port is needed on Android. It implements exactly what Wine needs:
  `wl_compositor`, `wl_subcompositor`, `wl_shm`, `xdg_wm_base`, `wp_viewporter`, `wl_seat` (pointer
  only), `wl_output`. It copies each committed buffer and composes the newest top-level window plus its
  subsurfaces into one ARGB picture. It runs in the app process and starts before any Wine process.
- **Wine**: built with `--with-wayland --with-freetype`. The prefix's graphics driver is
  `wayland,null`, so if the compositor isn't reachable Wine falls back to the old headless behaviour.
- **Wine patch `0002-winewayland-one-toplevel-per-process.patch`**: with
  `VSTGHOST_WL_ONE_TOPLEVEL=1`, every other visible window of the plugin's process (menus, dialogs)
  becomes a subsurface of our editor window at its real Windows position. Without it, JUCE's popup
  menus became separate top-level windows that a Wayland compositor has no position for. (Wine's
  virtual desktop would have solved this too, but the Wayland driver doesn't support it.)
- **Host**: `serve` gained `editor_open` / `editor_close` / `editor_state`. The editor window is
  a borderless popup at (0,0). Edits made in the editor (`IComponentHandler::performEdit`) are now
  forwarded to the audio thread; before, they were dropped.
- **No OpenGL**: Odin2's editor normally uses OpenGL. With no GL available, JUCE falls back to its
  software renderer, which looks the same (checked side by side on the PC). This avoids shipping an emulated
  x86_64 Mesa.
- **x86_64 libraries** (run emulated by Box64): libwayland-client, libffi, libxkbcommon, FreeType
  and its deps, 1.9 MB total, copied from the Ubuntu 24.04 build host with package versions in
  `x64libs/MANIFEST`. `libxkbregistry` is a 9-line stub (the real one pulls in libxml2 and ICU,
  ~30 MB). Wine only needs it for keymaps, which our compositor never sends.

## Measurements (device)

| | Sideload | Play |
|---|---|---|
| `editor_open` (create window → editor attached) | 7.4–7.5 s | 7.2 s |
| Picture | 1200×924 (Odin2 resizes itself from 800×616), 0 protocol errors | same |
| Tap on Osc 2 menu arrow → menu shown | yes (1 → 7 mapped surfaces) | yes |
| Tap outside → menu closed | yes | yes |
| Underruns while editor open (block 256) | 0 | 0 |
| Host CPU while editor idle | ~3 % of one core | — |

The editor picture was checked from the compositor's own output (`--ei dump`), not from the screen:
the phone was locked at the PIN screen during the test, so **nobody has seen it on the screen or touched it yet.**
On the PC (WSL), the same compositor with the same Wine showed Odin2 correctly, and a scripted
knob drag changed the Gain knob (1142 px changed, all within the knob).

## Not done / known gaps

- **On-screen use** (looking at it, real finger drags on knobs) not yet confirmed by a person.
- First open takes ~7.5 s (Odin2's GUI initialisation under emulation).
- No keyboard input to plugins (no `wl_keyboard`); no clipboard.
- WebView2 editors (E-Piano, Brass Knuckles, Pedal Faster): blank, as on the PC. Phase 5.
- Only one editor is shown at a time (the most recently opened).
- Editors opened from AAP hosts: not wired (open it from our app's player).
- The payload now reinstalls automatically when a different archive is available
  (`Payload.needsInstall()`); before, it never updated after the first install.
