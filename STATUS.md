# Status

_Last updated: 2026-11-18_

## Current phase

**Phase 3: implemented and verified on the device, with one acceptance check open** ([docs/phase3-report.md](docs/phase3-report.md)). The open check is playing a virtual MIDI device from a *third-party* MIDI app. Phases 0–2 are done. Next: close that check, then Phase 4 (plugin GUIs).

## What works (run on the Galaxy S26 Ultra SM-S948U1, Android 17, Play edition)

| Feature | Evidence |
|---|---|
| Wine 11.0 + Box64 inside our APK (Play targetSdk 36 and sideload) | Phase 0 |
| 4/4 test plugins load and render, bit-identical to native x86_64 | Phase 1 |
| Live playing: ~20–24 ms key → speaker at 128/256 blocks, 0 underruns; USB MIDI from a PC | Phase 2 |
| **8 AAP plugin slots** (Inst 1–4, FX 1–4); **aap-juce-simple-host plays a bridged VST** (E-Piano: notes in, audio out, 0 underruns) | Phase 3 |
| **4 virtual MIDI devices** ("VST gHost Inst 1–4"); 2 slots played at once via Android's MIDI API, both PASS | Phase 3 self-test |

## Measured numbers (device)

| Metric | Value |
|---|---|
| Key → speaker (player, phone speaker) | 20–24 ms at 128/256 frames, 33–35 ms at 512 |
| AAP slot cold start (host instantiate → Windows plugin running) | E-Piano 1.45 s, Odin2 3.3 s |
| Virtual MIDI device: first note → plugin running | 1.9 s (E-Piano), 2.7 s (Odin2) |
| AAP `process()` | 500/s at 96 frames, 0 underruns, 512-frame bridge buffer |
| APK size (Play, payload bundled) | 96.1 MB (was 75.6 MB before the AAP library) |

## Doesn't work / not verified yet

- **A third-party MIDI app → our virtual device:** not demonstrated (the device path is verified through the public API).
- **AAP hosts showing our runtime parameter list:** not checked. **FX slots through AAP:** not exercised.
- Plugin GUIs (Phase 4). No foreground service yet (background playing not guaranteed).
- Touch latency and acoustic round trip: no hardware.

## Open questions / risks

1. Memory: the Wine session plus one host per active slot (~150–230 MB each).
2. **Deviation:** glibc is Termux's prebuilt package; rebuild from source before a public release.
3. The Play edition's loader path is primary-user only.
4. The sideload edition now has minSdk 29 > targetSdk 28. It builds; reinstalling it on the phone hasn't been re-checked since the change.

## Assumptions still to prove

- Play review accepts the emulator-style user-VST feature.
