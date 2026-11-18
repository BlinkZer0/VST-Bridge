# VST gHost: Phase 2 report (real-time)

Generated 2026-11-18 10:50:20 on samsung SM-S948U1, Android 17, edition **play**. Measured on this device in this run.

Audio output: 48000 Hz, burst 96 frames, buffer 192/6144, AAudio, exclusive=true, low-latency=true; Oboe-reported output latency 12.3 ms

## Ring integrity under Box64
- 20 s at 128-frame blocks, host writes frame index as samples, engine checks every sample: **0 mismatches in 1919808 samples**, underruns 0, wakeups futex
- Box64 settings: defaults (no STRONGMEM); ring indices use seq_cst atomics

## E-Piano @ 128 frames
- buffer 2 × 128 = 256 frames: load 443 ms, underruns 0, Oboe xruns 0, host render avg 663 µs max 1494 µs, callback avg 17 µs max 566 µs, host CPU 30%
- latency probes (ms): 8.1, 8.1, 8.1, 8.1, 10.1, 10.1, 10.1, 10.1, 10.1, 10.1

## E-Piano @ 256 frames
- buffer 2 × 256 = 512 frames: load 403 ms, underruns 0, Oboe xruns 0, host render avg 1295 µs max 3303 µs, callback avg 17 µs max 1541 µs, host CPU 29%
- latency probes (ms): 10.8, 11.4, 11.4, 11.4, 12.1, 12.8, 12.8, 12.8, 14.1, 14.1

## E-Piano @ 512 frames
- buffer 2 × 512 = 1024 frames: load 403 ms, underruns 0, Oboe xruns 0, host render avg 2601 µs max 4498 µs, callback avg 16 µs max 2158 µs, host CPU 30%
- latency probes (ms): 23.4, 23.4, 24.1, 24.1, 24.8, 24.8, 28.8, 30.8, 30.8, 31.4

## Odin2 @ 128 frames
- buffer 2 × 128 = 256 frames: load 1187 ms, underruns 0, Oboe xruns 0, host render avg 1314 µs max 4777 µs, callback avg 16 µs max 1260 µs, host CPU 52%
- latency probes (ms): 8.2, 8.4, 10.1, 10.1, 10.1, 10.1, 10.3, 10.4, 10.4, 10.5

## Odin2 @ 256 frames
- buffer 2 × 256 = 512 frames: load 1168 ms, underruns 0, Oboe xruns 0, host render avg 2655 µs max 6714 µs, callback avg 13 µs max 2416 µs, host CPU 54%
- latency probes (ms): 10.9, 11.1, 11.5, 11.6, 11.9, 12.2, 12.8, 13.5, 14.1, 14.4

## Odin2 @ 512 frames
- buffer 2 × 512 = 1024 frames: load 1166 ms, underruns 0, Oboe xruns 0, host render avg 5260 µs max 10248 µs, callback avg 12 µs max 1165 µs, host CPU 53%
- latency probes (ms): 19.6, 21.4, 21.7, 21.8, 22.5, 22.6, 23.1, 24.4, 25.0, 25.2

## Latency and stability

| Plugin | Block | Smallest stable buffer | Bridge latency (note-on → output, median / max) | + Oboe output latency | ≈ key → speaker | Host CPU (% of one core) | Host render avg / max | Underruns / Oboe xruns |
|---|---|---|---|---|---|---|---|---|
| E-Piano | 128 | 2 × 128 requested, **384 frames used** (8.0 ms; callback 96, low-latency) | 10.1 / 10.1 ms | 12.3 ms | **22 ms** | 30% | 663 / 1494 µs | 0 / 0 |
| E-Piano | 256 | 2 × 256 requested, **512 frames used** (10.7 ms; callback 96, low-latency) | 12.8 / 14.1 ms | 11.0 ms | **24 ms** | 29% | 1295 / 3303 µs | 0 / 0 |
| E-Piano | 512 | 2 × 512 requested, **1024 frames used** (21.3 ms; callback 96, low-latency) | 24.8 / 31.4 ms | 10.4 ms | **35 ms** | 30% | 2601 / 4498 µs | 0 / 0 |
| Odin2 | 128 | 2 × 128 requested, **384 frames used** (8.0 ms; callback 96, low-latency) | 10.1 / 10.5 ms | 10.3 ms | **20 ms** | 52% | 1314 / 4777 µs | 0 / 0 |
| Odin2 | 256 | 2 × 256 requested, **512 frames used** (10.7 ms; callback 96, low-latency) | 12.2 / 14.4 ms | 11.3 ms | **23 ms** | 54% | 2655 / 6714 µs | 0 / 0 |
| Odin2 | 512 | 2 × 512 requested, **1024 frames used** (21.3 ms; callback 96, low-latency) | 22.6 / 25.2 ms | 10.6 ms | **33 ms** | 53% | 5260 / 10248 µs | 0 / 0 |

Bridge latency is measured inside the engine: frames between sending the note and the first output sample above −50 dBFS (includes the plugin's own attack). Oboe output latency is AAudio's estimate of buffer → speaker. Touch-screen latency is not included. No acoustic loopback was available.

## Crash isolation
- killed host pid 9686 with SIGKILL while playing
- app still running: yes (this report); audio callbacks continued: 1000 callbacks in the 2 s after the kill (PASS); host exit noticed: yes (code 137)
- restart + restore state: OK in 2872 ms; state restored: true; plays again: yes (probe 12.1 ms)

## Preset (state) round trip
- `Osc 1 On`: original 1.000, changed to 0.100, after restoring the saved state 1.000 → **PASS** (state 4844 base64 chars)

