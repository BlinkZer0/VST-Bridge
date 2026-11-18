# VST gHost: Phase 2 report (real-time)

Generated 2026-11-18 10:39:38 on samsung SM-S948U1, Android 17, edition **play**. Measured on this device in this run.

Audio output: 48000 Hz, burst 882 frames, buffer 1764/6272, AAudio, exclusive=false, low-latency=false; Oboe-reported output latency 209.4 ms

## Ring integrity under Box64
- 20 s at 128-frame blocks, host writes frame index as samples, engine checks every sample: **0 mismatches in 1921508 samples**, underruns 1, wakeups futex
- Box64 settings: defaults (no STRONGMEM); ring indices use seq_cst atomics

## E-Piano @ 128 frames
- buffer 2 × 128 = 256 frames: load 707 ms, underruns 0, Oboe xruns 0, host render avg 657 µs max 1355 µs, callback avg 59 µs max 1590 µs, host CPU 26%
- latency probes (ms): 42.9, 44.5, 44.6, 128.7, 149.5, 149.9, 150.3, 150.7, 151.0, 151.4

## E-Piano @ 256 frames
- buffer 2 × 256 = 512 frames: load 324 ms, underruns 0, Oboe xruns 0, host render avg 909 µs max 2569 µs, callback avg 42 µs max 392 µs, host CPU 18%
- latency probes (ms): 144.3, 147.0, 147.4, 147.8, 148.2, 148.5, 148.9, 148.9, 149.3, 149.3

## E-Piano @ 512 frames
- buffer 2 × 512 = 1024 frames: load 423 ms, underruns 0, Oboe xruns 0, host render avg 2478 µs max 5464 µs, callback avg 56 µs max 399 µs, host CPU 25%
- latency probes (ms): 151.3, 151.6, 152.0, 152.4, 152.8, 153.1, 153.5, 153.9, 154.3, 154.6

## Odin2 @ 128 frames
- buffer 2 × 128 = 256 frames: load 1229 ms, underruns 0, Oboe xruns 0, host render avg 1243 µs max 3786 µs, callback avg 57 µs max 1407 µs, host CPU 49%
- latency probes (ms): 130.3, 130.4, 130.6, 138.0, 138.1, 138.3, 138.5, 138.6, 138.8, 138.9

## Odin2 @ 256 frames
- buffer 2 × 256 = 512 frames: load 1190 ms, underruns 0, Oboe xruns 0, host render avg 2077 µs max 6639 µs, callback avg 52 µs max 2833 µs, host CPU 40%
- latency probes (ms): 131.4, 133.6, 144.1, 145.1, 146.9, 147.4, 147.6, 148.3, 149.1, 149.3

## Odin2 @ 512 frames
- buffer 2 × 512 = 1024 frames: load 1188 ms, underruns 0, Oboe xruns 0, host render avg 4710 µs max 11724 µs, callback avg 50 µs max 377 µs, host CPU 46%
- latency probes (ms): 139.1, 147.2, 147.7, 148.5, 151.3, 158.3, 158.7, 159.4, 159.6, 159.9

## Latency and stability

| Plugin | Block | Smallest stable buffer | Bridge latency (note-on → output, median / max) | + Oboe output latency | ≈ key → speaker | Host CPU (% of one core) | Host render avg / max | Underruns / Oboe xruns |
|---|---|---|---|---|---|---|---|---|
| E-Piano | 128 | 2 × 128 = 256 frames (5.3 ms) | 149.9 / 151.4 ms | 231.7 ms | **382 ms** | 26% | 657 / 1355 µs | 0 / 0 |
| E-Piano | 256 | 2 × 256 = 512 frames (10.7 ms) | 148.5 / 149.3 ms | 197.8 ms | **346 ms** | 18% | 909 / 2569 µs | 0 / 0 |
| E-Piano | 512 | 2 × 512 = 1024 frames (21.3 ms) | 153.1 / 154.6 ms | 201.2 ms | **354 ms** | 25% | 2478 / 5464 µs | 0 / 0 |
| Odin2 | 128 | 2 × 128 = 256 frames (5.3 ms) | 138.3 / 138.9 ms | 206.8 ms | **345 ms** | 49% | 1243 / 3786 µs | 0 / 0 |
| Odin2 | 256 | 2 × 256 = 512 frames (10.7 ms) | 147.4 / 149.3 ms | 201.0 ms | **348 ms** | 40% | 2077 / 6639 µs | 0 / 0 |
| Odin2 | 512 | 2 × 512 = 1024 frames (21.3 ms) | 158.3 / 159.9 ms | 204.8 ms | **363 ms** | 46% | 4710 / 11724 µs | 0 / 0 |

Bridge latency is measured inside the engine: frames between sending the note and the first output sample above −50 dBFS (includes the plugin's own attack). Oboe output latency is AAudio's estimate of buffer → speaker. Touch-screen latency is not included. No acoustic loopback was available.

## Crash isolation
- killed host pid 5540 with SIGKILL while playing
- app still running: yes (this report); audio callbacks continued: 109 callbacks in the 2 s after the kill (PASS); host exit noticed: yes (code 137)
- restart + restore state: OK in 3291 ms; state restored: true; plays again: yes (probe 137.5 ms)

## Preset (state) round trip
- `Osc 1 On`: original 1.000, changed to 0.100, after restoring the saved state 1.000 → **PASS** (state 4844 base64 chars)

