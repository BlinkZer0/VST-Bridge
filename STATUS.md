# Status

_Last updated: 2026-11-19_

## Current phase

**Phase 5 (native fast path + "most VST3s"): working on the device, Play edition** ([docs/phase5-report.md](docs/phase5-report.md)). The user's three JUCE plugins run natively (universal JUCE adapter, native editors with pinch-zoom); Windows plugins can be imported from files/zips, and the plugin types that failed before now work: JUCE 8 Direct2D editors (Dexed), JUCE 9 GDI editors, OpenGL editors (Surge XT). Still open in Phase 5: Odin2 native build (JUCE forbids modal loops on Android), `.jucer` projects, installer-only plugins, the rest of the test corpus on the phone, sideload edition re-test.
Phase 4 ([docs/phase4-report.md](docs/phase4-report.md)) remains verified in both editions.
Still open from Phase 3: a third-party MIDI app playing our virtual device.

## What works (run on the Galaxy S26 Ultra SM-S948U1, Android 17, Play edition)

| Feature | Evidence |
|---|---|
| Wine 11.0 + Box64 inside our APK (Play targetSdk 36 and sideload) | Phase 0 |
| 4/4 test plugins load and render, bit-identical to native x86_64 | Phase 1 |
| Live playing: ~20–24 ms key → speaker at 128/256 blocks, 0 underruns; USB MIDI from a PC | Phase 2 |
| **8 AAP plugin slots** (Inst 1–4, FX 1–4); **aap-juce-simple-host plays a bridged VST** (E-Piano: notes in, audio out, 0 underruns) | Phase 3 |
| **Plugin editors**: Odin2's own editor shown in the app (in-app Wayland compositor + Wine's Wayland driver), taps work (menus open/close), editor edits reach the audio, 0 underruns | Phase 4 |
| **4 virtual MIDI devices** ("VST gHost Inst 1–4"); 2 slots played at once via Android's MIDI API, both PASS | Phase 3 self-test |
| **Native plugins**: E-Piano, Brass Knuckles, Pedal Faster built from source with the JUCE adapter, 40–140 µs per 96-frame block, native editors (web UI included), MIDI self-test PASS | Phase 5 |
| **No Strings and The Ham native** (JUCE 8 + WebView2 web UIs, blank under Wine): built from their local checkouts (`-Pvg.pluginSrcRoot`, local-only entries in native/CMakeLists.txt), load in 4–8 ms (53 / 143 parameters), web UI editors complete and interactive. Editors are no longer shrunk below their logical size (the zoom layout scales them instead), which stops web pages from clipping | 2026-11-19 |
| **Imported Windows plugins**: Dexed (Direct2D editor complete, taps work), Pedal Faster Windows build (GDI editor), Surge XT (2081 parameters, OpenGL editor) — all with 0–1 underruns while the editor is open | Phase 5 |
| **Free-VST3 corpus on the phone** (2026-11-19): 30/30 import, load and run (0 underruns), 25/25 editors with a GUI open — incl. Monique, Surge XT Effects, wolf-shaper, the Dragonflys and the OpenGL visualizers that failed or were untested before ([docs/compatibility.md](docs/compatibility.md)) | `tools/corpus/phone-triage.sh` |
| **Release batch 2026-11-19**: proprietary licensing (notices + LGPL source offer in the app), About screen, setup-progress dialog, first-run card, Report a problem (zip via share sheet), audio focus, MIDI settings (channel filter, channel→slot routing, velocity curve), editor hidden while its screen is paused and closed after 30 s in the background (reopened on resume); **payload series 3 = WoW64** (Wine i386+x86_64, Box64 at 256 GB, UTF-8 file names): 32-bit installers run — Surge XT installed from its Inno Setup with factory data, nested modal loops for native JUCE plugins, Projucer `.jucer` projects in the adapter, 16 KB page alignment verified, glibc-from-source payload step | 2026-11-19 |
| **Play together**: a slot's long-press menu adds it to the group the keyboard/USB MIDI play at once (green = played, orange outline = the current slot for Editor/Presets/Unload); persisted. **Effect faders are wet amounts** (0–100 %): an effect never changes the chain's level, only how much of the signal it processes; the chain itself is serial in slot order (FX 1 → FX 2 → …). **Instrument sets** (⋮ menu): save/load/delete named sets = loaded plugins + their state + play group + mixer. Verified: two instruments play together, set saved and reloaded with all three plugins | 2026-11-19 |
| **Background playing**: a media-playback foreground service ("VST gHost is playing", Stop action) runs while any slot has a plugin; verified: notification shown, process alive after leaving the screen, Stop unloads all slots. **Prefix data kept across updates** (users, ProgramData, Common Files/VST3; marker file survived a reinstall). **Meters on the slot cards + Master strip** in the mixer. **Installers** (.exe/.msi) run unattended under Wine from Install plugin… (code only, not yet exercised) | 2026-11-19 |
| *(retired: the app ships through Play only; the sideload flavour stays buildable for troubleshooting)* **Sideload edition re-test** (targetSdk 28 build, payload pushed to its external files dir): Dexed imports, plays (−9.1 dBFS, 0 underruns), Direct2D editor complete (12 s); Surge XT plays (−3.8 dBFS), OpenGL editor 2.3 s | 2026-11-19 |
| **Mixer** (⋮ menu): eight strips (Inst 1–4, FX 1–4) with live peak meters (blue → teal → amber → red, peak hold), faders (−∞..+6 dB, mixer taper) and mutes; per-slot gain and meter live in the engine's audio callback; settings persist. **Uninstall plugin** (⋮ menu): removes an imported plugin's files after a confirmation, unloading it from any slot first. **App icon**: a ghost with headphones (adaptive vector icon, monochrome layer included) | 2026-11-19 |
| **Install plugin** (⋮ menu): lists `.vst3` files/bundles and zips in Download, Documents, the storage root and SD cards (needs "All files access"; the app asks once and opens the settings page) and installs the tapped one. Verified: Dexed zip, No Strings and The Ham bundles from Download; `.exe`/`.msi`/`.7z` are listed as not installable | 2026-11-19 |

## Measured numbers (device)

| Metric | Value |
|---|---|
| Key → speaker (player, phone speaker) | 20–24 ms at 128/256 frames, 33–35 ms at 512 |
| AAP slot cold start (host instantiate → Windows plugin running) | E-Piano 1.45 s, Odin2 3.3 s |
| Virtual MIDI device: first note → plugin running | 1.9 s (E-Piano), 2.7 s (Odin2) |
| AAP `process()` | 500/s at 96 frames, 0 underruns, 512-frame bridge buffer |
| Plugin editor open (Odin2) | 7.2–7.5 s |
| Plugin editor open: Dexed (Direct2D emulated) / Pedal Faster (GDI) / Surge XT (OpenGL) | 12–14 s / 1.9 s / 2.2 s |
| Host CPU with the editor open, idle: Dexed / Pedal Faster / Surge XT | ≈ 2.2 cores / 0.7 / 0.1 |
| Payload extraction on first start (series 2, Mesa included) | ≈ 2 min |
| APK size (Play, payload bundled) | 96.1 MB (was 75.6 MB before the AAP library) |

## Doesn't work / not verified yet

- **A third-party MIDI app → our virtual device:** not demonstrated (the device path is verified through the public API).
- **AAP hosts showing our runtime parameter list:** not checked. **FX slots through AAP:** not exercised.
- Editors: on-screen use not yet confirmed by a person; no keyboard input to plugins; WebView2 editors of bridged (Wine) plugins blank — the native builds of those plugins are the answer; editors not reachable from AAP hosts' UIs.
- Odin2 native build (modal loops); Projucer-only projects; plugins that need their installer's data (Surge XT has no factory patches without it).
- Direct2D-era JUCE 8 editors cost about two cores while open (software rasterization of every presented frame).
- Touch latency and acoustic round trip: no hardware.

## Open questions / risks

1. Memory: the Wine session plus one host per active slot (~150–230 MB each).
2. **Deviation:** glibc is Termux's prebuilt package; rebuild from source before a public release.
3. The Play edition's loader path is primary-user only.
4. **Deviation:** the x86_64 runtime libraries for Wine's Wayland driver/FreeType are copied from the Ubuntu 24.04 build host (versions in `x64libs/MANIFEST`), not built from source.
5. The sideload edition has minSdk 29 > targetSdk 28; reinstalled and re-checked 2026-11-19 (Dexed, Surge XT).

## Assumptions still to prove

- Play review accepts the emulator-style user-VST feature.
