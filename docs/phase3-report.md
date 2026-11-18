# Phase 3 report: bridged plugins exposed to other apps

**Result: a Windows VST3 synth, bridged by VST gHost, plays inside a third-party AAP host app (aap-juce-simple-host), and as an Android virtual MIDI device.** Both were measured on the S26 Ultra. One acceptance check is still open: playing a virtual MIDI device from a *third-party* MIDI app. The device path itself was verified through Android's public MIDI API from a self-test (details below).

Measured 2026-11-18 on SM-S948U1 (Android 17), Play edition, phone speaker.

## What other apps now see

| Exposed as | Names | How the VST is chosen |
|---|---|---|
| **AAP plugins** (Audio Plugins For Android 0.12.0) | VST gHost **Inst 1–4** (instruments), VST gHost **FX 1–4** (effects) | The user assigns any bridged VST to a slot in the app (**Slots** button). AAP plugin lists are fixed when the APK is built, so the slots are fixed and their contents change. |
| **Android virtual MIDI devices** | VST gHost **Inst 1–4** (input port), manufacturer "VST gHost" | Same slot assignments. The VST plays through VST gHost's own audio output. |

How a slot is served:
- AAP hosts bind to the AAP service declared in our manifest, and the AAP runtime calls our native plugin code (`libvstghost.so`, [aap_plugin.cpp](../app/src/main/cpp/aap_plugin.cpp)) **inside our app process**.
- `prepare()` starts the slot's Windows host for the assigned VST; `process()` converts AAP's MIDI 2.0 events to MIDI 1.0 for the bridge and reads the rendered audio from the shared-memory ring.
- Effects feed the host's audio input into the ring.
- The VST's real parameters are reported at runtime through AAP's parameters extension, and its state through the state extension.

## Results

### 1. AAP host: aap-juce-simple-host v0.9.0 (third-party app, from atsushieno's GitHub releases)

| Step | Result |
|---|---|
| *Scan plugins* | "VST gHost" vendor listed with all 8 slots (Inst 1–4, FX 1–4) |
| *Add* "VST gHost Inst 1" (assigned: E-Piano) | AAP `instantiate` → slot 1. Windows E-Piano host running under Box64 in **1.45 s** (cold); `prepare()` OK at 48 kHz |
| Notes from the host's on-screen keyboard | 11–14 MIDI 2.0 messages received and converted; **audio returned to the host, peak −22.5 dBFS** |
| Steady state | 500 `process()` calls per second (96 frames each), **0 underruns**, bridge buffer 512 frames (10.7 ms) |
| "VST gHost Inst 2" (Odin2) | Instantiated and prepared (3.3 s cold). Not played during that run. |

### 2. Virtual MIDI devices (self-test, [results/phase3-selftest-play.md](results/phase3-selftest-play.md))

The self-test opens our devices through `MidiManager.openDevice` and `openInputPort`, the same public API a third-party app uses, and plays **two slots at the same time**:

| Device | VST | Host started after the first note | MIDI consumed by the host | Output peak | Underruns | Result |
|---|---|---|---|---|---|---|
| VST gHost Inst 1 | E-Piano | 1.9 s | 96 | −14.2 dBFS | 0 | PASS |
| VST gHost Inst 2 | Odin2 | 2.7 s | 96 | −0.1 dBFS | 0 | PASS |

This also confirms **multi-slot mixing**: two Windows plugins at once, mixed by the engine with no underruns.

### 3. Not verified yet

- **A third-party MIDI app sending to our devices.** Neither installed app does this in an obvious way:
  - aap-juce-simple-host lists our devices' *input* ports under its *inputs* (a JUCE-on-Android quirk) and can't open them;
  - UAPMD's UI didn't reveal MIDI-device output routing.
  The device path itself is proven by the self-test above. **To close this:** any Play Store MIDI controller app that can pick an Android MIDI output; choose "VST gHost Inst 1".
- **Whether AAP hosts show our runtime parameter list.** We send it and notify the host, but haven't inspected a host's parameter UI.
- **The FX slots inside an AAP host.** The input-ring path is the same one the player's effect mode uses (tested in Phase 2), but it hasn't been exercised through AAP.

## Problems found during Phase 3 (all fixed)

1. **Merely opening our MIDI devices started Windows hosts.** JUCE hosts open every MIDI device just to list them, so every assigned slot booted Wine. Hosts now start on the first MIDI message.
2. **Race when two slots started at once:** concurrent runtime setup deleted the loader-symlink folder mid-walk and crashed the app. Setup now runs once per process under a lock, the symlinks are rebuilt only when missing, and slot-start failures are logged instead of crashing.
3. **Toolchain:** the AAP library requires compileSdk 37, which needs AGP ≥ 9.x. Moved to AGP 9.5.1, Gradle 9.7.1 (checksum-pinned) and AGP's built-in Kotlin. targetSdk stays 36 (Play) and 28 (sideload); minSdk is now 29 (the AAP library's minimum).

## Cost

The Play APK went from 75.6 MB to 96.1 MB. The AAP runtime is 0.94 MB of native code once stripped; the rest is AndroidX and Compose bytecode pulled in by the AAP library.
