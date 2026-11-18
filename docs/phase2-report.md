# Phase 2 report: real-time standalone player

**Result: Windows VST3 plugins play live on the S26 Ultra, about 20–24 ms from key to speaker at 128/256-frame blocks, with 0 underruns.** Plugins can be played from the on-screen keyboard or from a PC over USB MIDI. The player has parameter sliders, presets, crash isolation, and a stats/measurement panel.

Measured 2026-11-18 on SM-S948U1 (Android 17), Play edition, phone speaker. Raw report: [results/phase2-report-play.md](results/phase2-report-play.md). An earlier run over Bluetooth headphones is kept in [results/phase2-report-play-bluetooth.md](results/phase2-report-play-bluetooth.md).

## Latency and load (48 kHz, phone speaker: AAudio low-latency, exclusive, 96-frame bursts)

| Plugin | Block | Bridge buffer used | Bridge latency (note-on → output, median / max) | Android output latency | **≈ key → speaker** | Host CPU (one core) | Render per block avg / max | Underruns / xruns (15 s busy playing) |
|---|---|---|---|---|---|---|---|---|
| E-Piano | 128 | 384 frames (8.0 ms) | 10.1 / 10.1 ms | 12.3 ms | **22 ms** | 30% | 0.66 / 1.49 ms | 0 / 0 |
| E-Piano | 256 | 512 frames (10.7 ms) | 12.8 / 14.1 ms | 11.0 ms | **24 ms** | 29% | 1.30 / 3.30 ms | 0 / 0 |
| E-Piano | 512 | 1024 frames (21.3 ms) | 24.8 / 31.4 ms | 10.4 ms | **35 ms** | 30% | 2.60 / 4.50 ms | 0 / 0 |
| Odin2 | 128 | 384 frames (8.0 ms) | 10.1 / 10.5 ms | 10.3 ms | **20 ms** | 52% | 1.31 / 4.78 ms | 0 / 0 |
| Odin2 | 256 | 512 frames (10.7 ms) | 12.2 / 14.4 ms | 11.3 ms | **23 ms** | 54% | 2.66 / 6.71 ms | 0 / 0 |
| Odin2 | 512 | 1024 frames (21.3 ms) | 22.6 / 25.2 ms | 10.6 ms | **33 ms** | 53% | 5.26 / 10.25 ms | 0 / 0 |

How these were measured:
- **Bridge latency** is measured exactly inside the engine: audio frames between sending a note and the first output sample above −50 dBFS. That includes the plugin's own attack.
- **Android output latency** is AAudio's own estimate.
- **Not included:** touch-screen latency, and any acoustic verification. No loopback cable or MIDI keyboard was available.
- **Recommended setting:** 128 or 256 frames. At 512 the plugin's worst-case block (up to 10 ms for Odin2) approaches its budget (10.7 ms), even though no underruns occurred.

**Bluetooth** (Beats Studio Pro): Android gives no low-latency path (882-frame callbacks, about 200 ms output latency). The engine detects this and raises its buffer automatically, giving 0 underruns over 15 s of busy playing. Total latency is dominated by Bluetooth itself.

## Correctness and robustness

| Test | Result |
|---|---|
| Ring integrity under Box64: the host writes a frame counter, the engine checks every sample (20 s, 128-frame blocks) | **0 mismatches in 1,919,808 samples.** No Box64 `STRONGMEM` needed: `seq_cst` atomics suffice. |
| Wakeups from the Windows host | Raw `futex` syscall from PE code works under Box64 (`wakeMode=futex`) |
| Crash isolation: SIGKILL the plugin host mid-play | **PASS**: audio kept running (1000 callbacks in the next 2 s), exit noticed, restart with state restored in 2.9 s, plays again |
| Presets: get state → change a parameter → set state | **PASS** (parameter back to its saved value) |
| USB MIDI in from a PC (phone in USB-MIDI mode, Windows output "MIDI function") | **Works**: a scale, chords and 15 s of busy playing (156 note-ons) arrived and played |

## What was built

- **Live host mode** (`vstghost-host serve`):
  - a render thread that keeps a shared-memory ring filled to a target;
  - pre-roll after loading, so Box64 translates the DSP code before anyone plays;
  - a control mailbox: parameter list, set parameter, get/set state;
  - exit when the app dies.
- **IPC** ([shared/ipc/vg_ipc.h](../shared/ipc/vg_ipc.h)), decided by measurement:
  - one file in app storage, mapped by both sides; Wine maps writable file sections `MAP_SHARED`;
  - wakeups by Linux futex, which the Windows host issues as a raw `syscall` (Wine x86_64 doesn't intercept it; Box64 translates it).
- **Engine** (Oboe):
  - the callback never waits on the host;
  - its buffer target follows the audio route's callback and buffer sizes, plus self-healing growth after real underruns;
  - multi-slot mixing, ready for Phase 3.
- **Player app:**
  - plugin picker, with the plugin header shown;
  - multi-touch keyboard (velocity from touch position), sustain, octave shift;
  - searchable parameter sliders, with JUCE's 2,080 "MIDI CC" pseudo-parameters filtered out;
  - presets;
  - stats panel with a latency test;
  - effect test signal;
  - crash banner with one-tap restart (state restored).
- **MIDI input:** any Android MIDI device (USB, Bluetooth, virtual). Tested with USB from a PC.

## Problems found during Phase 2 (all fixed)

1. **Crackling on Bluetooth:** 882-frame callbacks against a 512-frame bridge buffer. Fixed by route-aware buffer sizing plus self-healing growth.
2. **Over-grown buffer:** the empty-ring period at host start counted as underruns. Underruns now count only once the ring has first filled.
3. **Hang on cold start:** `wineserver -p` daemonizes but keeps stdout open. Its output is now discarded.
4. **Parameters invisible** in landscape: the layout and title bar were rebuilt.
5. **Android's `Process.waitFor(timeout)` polls every 100 ms** (found in Phase 0); avoided here too.

## Open / next

- AAP plugin service and virtual MIDI devices (Phase 3).
- A foreground service for playing in the background, not done yet.
- Touch-to-sound latency and acoustic verification need hardware we don't have.
