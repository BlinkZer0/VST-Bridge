# Status

_Last updated: 2026-11-18_

## Current phase

**Phase 2: done and demoed on the device** ([docs/phase2-report.md](docs/phase2-report.md)). Phases 0 and 1 are done. Next: Phase 3 (AAP plugin service + virtual MIDI devices). Its dependency, the AAP library 0.12.0, is approved, and so is installing aap-juce-simple-host and UAPMD for testing.

## What works (run on the Galaxy S26 Ultra SM-S948U1, Android 17)

| Feature | Evidence |
|---|---|
| Wine 11.0 + Box64 inside our APK, Play edition (targetSdk 36) and sideload | Phase 0 report |
| 4/4 test plugins load and render; audio bit-identical to native x86_64 | Phase 1 report |
| **Live playing:** ~20–24 ms key → speaker at 128/256 blocks, 0 underruns | Phase 2 report |
| On-screen keyboard; **USB MIDI from a PC** (phone appears as "MIDI function") | Phase 2 report |
| Parameter sliders (real parameters only), presets, crash isolation with state restore | Phase 2 report |
| Bluetooth output: auto-sized buffer, 0 underruns (latency dominated by Bluetooth) | Phase 2 report |

## Measured numbers (device, phone speaker, 48 kHz)

| Metric | Value |
|---|---|
| Key → speaker (bridge + Android output) | E-Piano 22 / 24 / 35 ms, Odin2 20 / 23 / 33 ms at 128 / 256 / 512 frames |
| Host CPU (one core) | E-Piano ~30%, Odin2 ~53% |
| Plugin load in a running Wine session (player) | 0.3–0.4 s (Odin2 ~1.2 s) |
| Ring integrity under Box64 | 0 / 1.9 M samples wrong |
| Crash → restarted with state | 2.9 s |

## Doesn't work / not attempted yet

- AAP plugin service, virtual MIDI devices (Phase 3). Plugin GUIs (Phase 4).
- No foreground service yet, so playing in the background isn't guaranteed.
- Touch latency and acoustic round trip: no hardware available.

## Open questions / risks

1. Wine session plus one host per plugin costs memory (~300–400 MB plus ~150–230 MB per plugin).
2. **Deviation:** glibc is Termux's prebuilt package; rebuild from source before a public release.
3. Play edition loader path is primary-user only.

## Assumptions still to prove

- aap-juce-simple-host accepts parameters reported at runtime (Phase 3).
- Play review accepts the emulator-style user-VST feature.
