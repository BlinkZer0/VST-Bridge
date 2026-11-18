# Status

_Last updated: 2026-11-18_

## Current phase

**Phase 4 (plugin editors): done and verified on the device in both editions** ([docs/phase4-report.md](docs/phase4-report.md)), except that a person has not yet used it on the screen (tested with picture dumps + injected taps while the phone was locked). **Next: Phase 5** (native fast path: the user's JUCE plugins as native AAP plugins with native UIs).
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

## Measured numbers (device)

| Metric | Value |
|---|---|
| Key → speaker (player, phone speaker) | 20–24 ms at 128/256 frames, 33–35 ms at 512 |
| AAP slot cold start (host instantiate → Windows plugin running) | E-Piano 1.45 s, Odin2 3.3 s |
| Virtual MIDI device: first note → plugin running | 1.9 s (E-Piano), 2.7 s (Odin2) |
| AAP `process()` | 500/s at 96 frames, 0 underruns, 512-frame bridge buffer |
| Plugin editor open (Odin2) | 7.2–7.5 s |
| APK size (Play, payload bundled) | 96.1 MB (was 75.6 MB before the AAP library) |

## Doesn't work / not verified yet

- **A third-party MIDI app → our virtual device:** not demonstrated (the device path is verified through the public API).
- **AAP hosts showing our runtime parameter list:** not checked. **FX slots through AAP:** not exercised.
- Editors: on-screen use not yet confirmed by a person; no keyboard input to plugins; WebView2 editors blank (Phase 5); editors not reachable from AAP hosts' UIs.
- No foreground service yet (background playing not guaranteed).
- Touch latency and acoustic round trip: no hardware.

## Open questions / risks

1. Memory: the Wine session plus one host per active slot (~150–230 MB each).
2. **Deviation:** glibc is Termux's prebuilt package; rebuild from source before a public release.
3. The Play edition's loader path is primary-user only.
4. **Deviation:** the x86_64 runtime libraries for Wine's Wayland driver/FreeType are copied from the Ubuntu 24.04 build host (versions in `x64libs/MANIFEST`), not built from source.
5. The sideload edition now has minSdk 29 > targetSdk 28. It builds; reinstalling it on the phone hasn't been re-checked since the change.

## Assumptions still to prove

- Play review accepts the emulator-style user-VST feature.
