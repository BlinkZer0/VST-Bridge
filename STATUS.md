# Status

_Last updated: 2026-11-18_

## Current phase

**Phase 0: done and demoed on the device** (see [docs/phase0-report.md](docs/phase0-report.md)). Next: Phase 1 (offline render with real VST3 plugins).

## What works (run on the Galaxy S26 Ultra, SM-S948U1, Android 17)

| Feature | Play edition (targetSdk 36) | Sideload edition (targetSdk 28) |
|---|---|---|
| Payload extraction from APK asset / Android/data | ✅ 1.95 s | ✅ 1.94 s |
| Box64 0.4.4 launches | ✅ (via the fixed-path PT_INTERP symlink) | ✅ (PT_INTERP rewritten at extraction) |
| Wine 11.0 runs an x86_64 Windows exe; output captured in the app | ✅ | ✅ |
| Box64 self re-exec for Wine children (wineserver, services…) | ✅ | ✅ |
| SELinux denials / phantom-process kills | 0 / 0 | 0 / 0 |

Verified only on the dev PC (x86_64 WSL, not the phone): the payload's Wine + prefix run hello.exe natively (reference checksum).

## Measured numbers (device, 2026-11-18)

| Metric | Play | Sideload |
|---|---|---|
| Payload: compressed / on disk | 63.2 / 407.5 MB | same |
| APK size | 75.6 MB (payload bundled) | 2.8 MB |
| Cold Wine session → hello.exe first output | 1074 ms | 1087 ms |
| In running session → first output | 93–104 ms | 104–139 ms |
| Guest processes per session | 7–8 | 7–8 |
| Peak summed RSS (overcounts shared pages) | 440 MB cold / 380 MB in-session | 445 / 382 MB |
| DSP kernel native vs Box64 | 82.0 vs 97.1 ms (1.18×) | 82.4 vs 96.7 ms (1.17×) |
| Float results native vs Box64 | identical | identical |

## Doesn't work / not attempted yet

- No VST loading yet (Phase 1). No audio I/O, MIDI or IPC yet (Phase 2).
- Play edition loader path is primary-user only (`/data/data/com.vstghost/...`).
- Memory is RSS, not PSS.

## Open questions / risks

1. Wine session services cost ~1.1 s and ~300–400 MB. Plan: one long-lived session plus trimmed services (Phase 1/2).
2. Box64 overhead on real SIMD-heavy plugin code is unknown (the Phase 0 kernel is scalar).
3. **Deviation:** glibc is Termux's prebuilt package, not rebuilt from source. Rebuilding is a TODO before a public release.
4. The phone runs Android 17, not 16 as the brief says. Everything above was measured on 17.

## Assumptions still to prove

- Wine runs headless (null display driver) for real plugin loading (Phase 1).
- Render thread can lower its nice value and pin to performance cores (Phase 2).
- `STRONGMEM=1` + `seq_cst` indices make the shared ring safe under Box64 (Phase 2 stress test).
- aap-juce-simple-host accepts parameters reported at runtime (Phase 3).
- Play review accepts the emulator-style user-VST feature.
