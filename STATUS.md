# Status

_Last updated: 2026-11-18_

## Current phase

**Phase 1: done and demoed on the device** ([docs/phase1-report.md](docs/phase1-report.md)). Phase 0 is done ([docs/phase0-report.md](docs/phase0-report.md)). Next: Phase 2 (real-time standalone app).

## What works (run on the Galaxy S26 Ultra SM-S948U1, Android 17, both editions unless noted)

| Feature | Evidence |
|---|---|
| Payload install (Wine 11.0 + Box64 0.4.4 + glibc) from APK asset (Play) or Android/data (sideload) | 1.9 s, Phase 0 report |
| Wine runs x86_64 Windows programs; Play edition at targetSdk 36 with 0 SELinux denials | Phase 0 report |
| Bridge host `vstghost-host.exe`: `scan` / `info` / `render` (MIDI → 32-bit float WAV, effects get a test signal) | Phase 1 report |
| **4/4 test plugins load and render**: E-Piano, Brass Knuckles, Pedal Faster (effect), Odin2 | Phase 1 report |
| **Rendered audio bit-identical to native x86_64** (all 4 plugins × both editions) | `tools/compare_wav.py`, max diff 0 |
| MIDI CC / pitch bend / sustain routed via `IMidiMapping` (412/412 mapped) | Phase 1 report |

## Measured numbers (device, 2026-11-18)

| Metric | Value |
|---|---|
| Payload: compressed / on disk | 64 / 414 MB |
| Cold Wine session → first output | ~1.08 s; in-session process start ~0.1 s |
| Plugin load in a running session | 122–178 ms (Odin2 894 ms) |
| Offline render speed, one core, 256-frame blocks | 27× (Odin2) to 82× (E-Piano) real time |
| Block time p99 / worst (budget 5.33 ms @ 256/48k) | 0.11–0.44 ms / 2.2–8.9 ms (early-render spikes) |
| Host process RSS per plugin | 149–228 MB |
| Estimated Box64 cost on real plugin DSP (phone vs PC) | ~2.3–3.3× (CPUs differ; estimate) |
| User-facing params (plus 2,080 JUCE MIDI-CC params each) | 98 / 349 / 146 / 259 |

## Doesn't work / not attempted yet

- No real-time audio, live MIDI, IPC or latency numbers yet (Phase 2).
- No plugin GUIs (Phase 4). The three newer plugins' editors are WebView-based (likely need WebView2 under Wine).
- Play edition loader path is primary-user only. Memory is RSS, not PSS.

## Open questions / risks

1. **Phase 2 IPC transport** between the MinGW PE host and the arm64 engine (see ARCHITECTURE.md §1); to be decided by measured wakeup latency.
2. **First-run block spikes** (up to 8.9 ms): likely Box64 translating code the first time it runs. Plan: pre-roll after load, plus ring prefill.
3. Wine session cost (~1.1 s start, ~300–400 MB) plus ~150–230 MB per plugin: consider trimming services and grouping hosts.
4. **Deviation:** glibc is Termux's prebuilt package; rebuild from source before a public release.
5. The brief says Android 16; the phone runs Android 17. All results are from 17.

## Assumptions still to prove

- Render thread can lower its nice value and pin to performance cores (Phase 2).
- `STRONGMEM=1` + `seq_cst` indices make the shared ring safe under Box64 (Phase 2 stress test).
- aap-juce-simple-host accepts parameters reported at runtime (Phase 3).
- Play review accepts the emulator-style user-VST feature.
