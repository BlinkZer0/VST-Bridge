# VST gHost: Phase 1 report (offline render)

## Compatibility

| Plugin | Type | Loads | Params | Load (ms) | Renders | Render speed (× real time) | Block p99 / max (ms, budget 5.33) | Output peak (dBFS) | Host peak RSS | Notes |
|---|---|---|---|---|---|---|---|---|---|---|
| Brass Knuckles | instrument | ✅ | 2429 | 125.3 | ✅ | 56.3 | 0.14 / 8.90 | -9.1 / -8.0 | 153 MB | - |
| E-Piano | instrument | ✅ | 2178 | 140.7 | ✅ | 79.2 | 0.15 / 5.09 | -9.9 / -9.9 | 144 MB | - |
| Odin2 | instrument | ✅ | 2339 | 884.7 | ✅ | 27.4 | 0.44 / 2.21 | 2.0 / 2.0 | 224 MB | - |
| Pedal Faster | effect | ✅ | 2226 | 160.8 | ✅ | 33.1 | 0.17 / 2.50 | -5.1 / -6.0 | 167 MB | - |

WAVs: `/storage/emulated/0/Android/data/com.vstghost.sideload/files/phase1-out` (pull with adb, compare to the PC reference with `tools/compare_wav.py`).

Generated 2026-11-18 09:58:29 on samsung SM-S948U1, Android 17, edition **sideload**. All numbers measured on this device in this run.

- payload: `s0-20261118-box64-0.4.4-wine-11.0-glibc-glibc-2.44-0`
- plugins: `Brass Knuckles.vst3`, `E-Piano.vst3`, `Odin2.vst3`, `Pedal Faster.vst3` (copied 2 files, 6.8 MB in 23 ms)
- Wine session start + `scan`: 1860 ms (exit 0)
- render: test MIDI `tests/phase1.mid` (20 s + 3 s tail), 48 kHz, block 256, VST3 offline mode; effects get the host's built-in 8 s test signal (+3 s tail)

## Brass Knuckles
- BrassKnuckles 1.0.0, VST 3.8.0, `Instrument|Synth`
- buses: in none, out Output (2ch), events 1; separate controller: true, IMidiMapping: true
- load 125.3 ms (LoadLibrary 62.8 ms), unload 19.2 ms; `info` process wall 640 ms
- 2429 parameters, e.g. Instrument = Trumpet, Engine = Modeled, Patch Level = 0.0 dB, Master = 70%, Fine Tune = 0 ct, Transpose = 0 st, Polyphony = 32, Voice Mode = Poly
- render: 23.0 s of audio in 408.8 ms → **56.3× real time**; block p50/p99/max 0.098 / 0.14 / 8.90 ms (budget 5.33 ms)
- notes 60, CC/bend/aftertouch mapped 412, unmapped 0; peak -9.1 / -8.0 dBFS; non-finite samples 0
- host process peak RSS 152.7 MB; `render` process wall 1080 ms

## E-Piano
- EPiano 0.9.0, VST 3.8.0, `Instrument|Synth`
- buses: in none, out Output (2ch), events 1; separate controller: true, IMidiMapping: true
- load 140.7 ms (LoadLibrary 82.2 ms), unload 20.1 ms; `info` process wall 658 ms
- 2178 parameters, e.g. Osc 1 On = On, Osc 1 Wave = Sine, Osc 1 Pitch = 0 st, Osc 1 Fine = 0 ct, Osc 1 Level = 70%, Osc 2 On = Off, Osc 2 Wave = Sine, Osc 2 Pitch = 0 st
- render: 23.0 s of audio in 290.5 ms → **79.2× real time**; block p50/p99/max 0.082 / 0.15 / 5.09 ms (budget 5.33 ms)
- notes 60, CC/bend/aftertouch mapped 412, unmapped 0; peak -9.9 / -9.9 dBFS; non-finite samples 0
- host process peak RSS 144.0 MB; `render` process wall 965 ms

## Odin2
- TheWaveWarden 2.4.0, VST 3.7.2, `Instrument|Synth`
- buses: in none, out Output (2ch), events 1; separate controller: true, IMidiMapping: true
- load 884.7 ms (LoadLibrary 59.2 ms), unload 16.1 ms; `info` process wall 1402 ms
- 2339 parameters, e.g. Osc1 Octave = 0, Osc1 Semitones = 0, Osc1 Finetune = 0.0000000, Osc1 Volume = 0.0000000, Osc1 Reset = 0, Osc1 Wavetble Position = 0.0000000, Osc1 Detune = 0.2000000, Osc1 Position Modulation = 0.0000000
- render: 23.0 s of audio in 838.6 ms → **27.4× real time**; block p50/p99/max 0.121 / 0.44 / 2.21 ms (budget 5.33 ms)
- notes 60, CC/bend/aftertouch mapped 412, unmapped 0; peak 2.0 / 2.0 dBFS; non-finite samples 0
- host process peak RSS 224.0 MB; `render` process wall 2231 ms

## Pedal Faster
- Pedal Faster 1.0.0, VST 3.8.0, `Fx|Distortion|Delay|Reverb`
- buses: in Input (2ch), out Output (2ch), events 1; separate controller: true, IMidiMapping: true
- load 160.8 ms (LoadLibrary 75.0 ms), unload 47.9 ms; `info` process wall 692 ms
- 2226 parameters, e.g. Input Gain = 0.0 dB, Input Mode = Stereo, Global Bypass = Off, Gate On = Off, Gate Threshold = -60.0, Gate Release = 80.0, Preset Output = 0.0 dB, Limiter On = On
- render: 23.0 s of audio in 694.2 ms → **33.1× real time**; block p50/p99/max 0.164 / 0.17 / 2.50 ms (budget 5.33 ms)
- notes 60, CC/bend/aftertouch mapped 412, unmapped 0; peak -5.1 / -6.0 dBFS; non-finite samples 0
- host process peak RSS 166.8 MB; `render` process wall 1418 ms

