# Phase 5 report: native fast path and "most VST3s"

_2026-11-19. Device: Galaxy S26 Ultra (SM-S948U1), Android 17, Play edition._

## Result

Two ways to run a plugin now exist side by side, and a slot picks one by its key:

1. **Native (`vgn_*` slots):** open-source JUCE plugins built for arm64 with a *universal JUCE
   adapter*, running inside the app process. The user's three plugins (E-Piano, Brass Knuckles,
   Pedal Faster) render a 96-frame block in 40–140 µs and show their own editors as native views
   (pinch-zoom, web UIs included).
2. **Bridged (Wine + Box64):** any Windows `.vst3`, imported from a file, a zip or a folder. This
   phase made that path work for the plugin types that failed before: JUCE 8 Direct2D editors
   (Dexed), JUCE 9 GDI editors (Pedal Faster's Windows build), OpenGL editors (Surge XT), and
   plugins whose DLLs the Play edition could not even load.

Verified on the phone today: Dexed (editor complete, keys/knobs/LCD, taps work), Pedal Faster
(Windows build, 1400×860 editor), Surge XT (engine with 2081 parameters, full OpenGL editor).

## The native path: universal JUCE adapter

`native/juce-shim/` re-implements JUCE's CMake API (`juce_add_plugin`, `juce_add_binary_data`,
`juce_generate_juce_header`, module targets) so an unmodified JUCE plugin project builds as
`libvgn_<target>.so` against JUCE 9.0.5 for Android. `native/CMakeLists.txt` lists projects as
pinned archives or git commits (`vg_add_juce_project`); `-Pvg.nativePlugins=a;b` limits the build.
The `vgn` C interface (`native/vgn/vgn.h`) gives the engine create/prepare/process/params/state/
editor; native slots render in the Oboe callback and in AAP `process()` with the same request
protocol the Wine host uses.

JUCE-on-Android details that cost time: `JNIClassBase::initialiseAllClasses` must run before
`Thread::initialiseJUCE`; `editorBeingDeleted()` must be called before deleting an editor (or the
second open says "no editor"); WebView editors need JUCE's bridge script injected at document start
(androidx.webkit `addDocumentStartJavaScript`), otherwise their controls (E-Piano's instrument
selector) never appear.

Not done: **Odin2** does not build natively yet — JUCE hard-codes `JUCE_MODAL_LOOPS_PERMITTED 0` on
Android and Odin2 calls `PopupMenu::show()` / `AlertWindow::showOkCancelBox()`; its 258-parameter
layout also exceeds clang's fold-expression limit. It runs through Wine. Projucer-only (`.jucer`)
projects are not supported by the adapter yet.

## The bridged path: what was broken and what fixed it

| Symptom on the phone | Cause | Fix |
|---|---|---|
| Every plugin importing `dwrite.dll` (or any DLL with relocations in `.text`) aborted on load: `free(): invalid pointer` inside Box64 | Wine maps PE sections from the file, writes relocations (copy-on-write) and re-enables execute. SELinux denies that (`execmod`) for apps targeting SDK ≥ 29; the Termux glibc's `mprotect()` wrapper then crashes in its `EACCES` path | Wine patch **0004**: `VSTGHOST_IMAGE_COPY=1` reads sections into anonymous memory |
| JUCE 8 (Direct2D) editors blank | No DirectComposition, no composition swap chains, `WaitForVBlank` E_NOTIMPL, dwrite rebuilt its font collection on every draw, no Verdana | Wine patch **0003**: minimal dcomp, `CreateSwapChainForComposition` presenting into the target window with GDI, vblank, dwrite cache; font replacements → Tahoma; Mesa llvmpipe for the D3D11 feature level |
| Dexed: first frame complete, then keys/knobs/LCD gone | JUCE clears the back buffer, repaints only dirty rects and passes them to `Present1`; DXGI keeps the rest of the window — our present copied the whole cleared buffer | Patch 0003: dirty rects honoured (persistent front buffer, dirty-box readback), `VSTGHOST_VBLANK_MS` |
| Dexed used 2.4 cores with the editor open | whole-frame readback through llvmpipe at 60 Hz | 20 Hz cap + dirty-box readback; still ≈ 2 cores because each presented frame is rasterized in software (see below) |
| Surge XT: only Bypass + MIDI CC parameters | Box64 mistranslates AVX code | `BOX64_AVX=0 BOX64_AVX2=0` for all x86 code |
| Surge XT editor: red "Access denied: Z:\data\user\0\SurgeXTData" page, later an uncaught exception | Surge walks up from its own path looking for a portable data folder; `/data/user/0` cannot be stat'ed by an app, and MSVC's `std::filesystem` throws on access-denied | plugins live on `D:` (`dosdevices/d:` → `files/plugins`, host maps paths to `D:\…`); patch 0004 `VSTGHOST_DENIED_IS_MISSING=1` reports such paths as "not found" |

**Software rendering is the rule.** JUCE 9 and JUCE 8 builds from 2026-02 on choose their GDI
renderer under Wine by themselves. Older JUCE 8 plugins (Dexed 1.1.0) choose Direct2D
unconditionally and paint nothing if it fails, so for them Direct2D is emulated through
d3d11 → wined3d → OpenGL → llvmpipe (Mesa 25.0.7 built for x86_64, run by Box64). That works but
costs about two cores while such an editor is open, because every presented frame is rasterized in
software; the vblank cap (20 Hz) bounds it. Real OpenGL editors (Surge XT, DPF plugins) use the same
llvmpipe and are cheap when idle (Surge XT: ~11 % of a core).

## Measurements (device, Play edition)

| | Dexed (JUCE 8, Direct2D) | Pedal Faster (JUCE 9, GDI) | Surge XT (OpenGL) |
|---|---|---|---|
| Editor open | 12–14 s | 1.9 s | 2.2 s |
| Picture | 866×674 complete, taps change the LCD | 1400×860 complete | 1141×711 complete |
| Host CPU, editor open, idle | ≈ 2.2 cores (main + 2 llvmpipe threads) | ≈ 0.7 core | ≈ 0.1 core |
| Audio while editor open (block 256) | 0–1 underruns | 0 | 0 |
| Native plugins, 96-frame block | E-Piano 40–52 µs, Brass Knuckles 67 µs, Pedal Faster 140 µs | | |

Payload series 2: Wine 11.0 (patches 0001–0004), Box64 0.4.4, Mesa 25.0.7 llvmpipe (static LLVM 18),
Termux glibc 2.44. APK extraction on first start takes about two minutes now.

## Still open

- Odin2 native build (modal loops); `.jucer` projects; installer-only plugins (Surge XT's factory
  data normally comes from its installer — without it the editor works but has no factory patches).
- Corpus on the phone: Wolf Shaper, Dragonfly, Surge XT Effects (crashed before today's fixes),
  Monique (hung) not re-tested yet.
- Direct2D editors could present only the dirty box to GDI as well; a newer Box64 may remove the
  need for `BOX64_AVX=0`.
