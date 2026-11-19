# Plugin compatibility

Free Windows VST3 plugins run through VST gHost's Wine/Box64 path. The corpus is fetched by
`tools/corpus/fetch.sh` (manifest: `tools/corpus/corpus.txt`); `tools/corpus/pc-triage.sh` runs
each plugin on the PC with the phone's configuration (our Wine with the Wayland driver into our
compositor, llvmpipe OpenGL, the same patches and environment) and `tools/corpus/phone-triage.sh`
repeats the load, a self-test and an editor picture through the app on the phone.

_PC results: 2026-11-19, payload series 2 (Wine 11.0 + patches 0001–0004, Box64 0.4.4, Mesa 25.0.7)._
_Phone column: pending (the phone pass needs about an hour of exclusive phone time)._

**Audio:** 30 of 30 load and render (`×realtime` = how many times faster than real time the PC
rendered 20 s at 48 kHz, block 256). AmplitudeImposer and Stochas render silence by design (a
sidechain effect without a second input; a MIDI sequencer). **Editors:** 25 show a picture;
MaBitcrush/MaFreeverb/MaGigaverb/MaPitchshift and Kars ship without a GUI. Five hosts did not exit
within 90 s after the editor was closed (Dexed, ProM, glBars, DragonflyEarlyReflections, wolf-shaper)
— the editor itself had rendered; the close path is to be looked at.

| Plugin | Source | Kind | Params | Audio (peak dBFS, ×realtime) | Editor (PC) |
|---|---|---|---|---|---|
| ADLplug | adlplug | instrument | 2152 | -11.3, 680.9 | yes (32 ms) |
| BasicLimiter | Uhhyou | effect | 9 | -5.9, 747.6 | yes (189 ms) |
| CollidingCombSynth | Uhhyou | instrument | 74 | -15.6, 17.7 | yes (212 ms) |
| CubicPadSynth | Uhhyou | instrument | 1564 | -5.5, 28.9 | yes (202 ms) |
| Dexed | dexed | instrument | 2264 | -3.3, 383.1 | yes; host hung on close |
| 3BandEQ | dpf-plugins | effect | 9 | -5.9, 1545.3 | yes (231 ms) |
| 3BandSplitter | dpf-plugins | effect | 9 | -13.9, 1430.8 | yes (230 ms) |
| AmplitudeImposer | dpf-plugins | effect | 5 | silent, 1505.5 | yes (142 ms) |
| CycleShifter | dpf-plugins | effect | 5 | -0.7, 2137.6 | yes (130 ms) |
| Kars | dpf-plugins | instrument | 2083 | 8.3, 1526.9 | no GUI (plugin has none) |
| MVerb | dpf-plugins | effect | 12 | -6.4, 338.2 | yes (204 ms) |
| MaBitcrush | dpf-plugins | effect | 1 | -3.5, 1594.0 | no GUI (plugin has none) |
| MaFreeverb | dpf-plugins | effect | 4 | -17.1, 599.6 | no GUI (plugin has none) |
| MaGigaverb | dpf-plugins | effect | 8 | -2.5, 469.4 | no GUI (plugin has none) |
| MaPitchshift | dpf-plugins | effect | 4 | -5.9, 161.8 | no GUI (plugin has none) |
| Nekobi | dpf-plugins | instrument | 2090 | -17.8, 551.7 | yes (178 ms) |
| PingPongPan | dpf-plugins | effect | 5 | -5.9, 525.8 | yes (213 ms) |
| ProM | dpf-plugins | effect | 0 | -5.9, 2011.8 | yes; host hung on close |
| SoulForce | dpf-plugins | effect | 7 | -0.8, 1002.3 | yes (120 ms) |
| glBars | dpf-plugins | effect | 5 | -5.9, 2332.1 | yes; host hung on close |
| DragonflyEarlyReflections | dragonfly | effect | 9 | -7.6, 506.0 | yes; host hung on close |
| DragonflyHallReverb | dragonfly | effect | 20 | -7.3, 85.5 | yes (174 ms) |
| DragonflyPlateReverb | dragonfly | effect | 11 | -7.9, 585.1 | yes (173 ms) |
| DragonflyRoomReverb | dragonfly | effect | 19 | -7.9, 89.6 | yes (191 ms) |
| MoniqueMonosynth | monique | instrument | 2434 | -3.3, 26.3 | yes (465 ms) |
| OPNplug | opnplug | instrument | 2152 | -13.5, 162.6 | yes (31 ms) |
| Stochas | stochas | instrument | 2137 | silent, 7189.1 | yes (492 ms) |
| Surge XT Effects | surge-xt | effect | 14 | -7.4, 959.2 | yes (479 ms) |
| Surge XT | surge-xt | instrument | 2855 | 0.3, 166.8 | yes (710 ms) |
| wolf-shaper | wolf-shaper | effect | 13 | -5.9, 561.7 | yes; host hung on close |

## Verified on the phone so far (Galaxy S26 Ultra, Play edition)

| Plugin | Audio | Editor |
|---|---|---|
| Dexed (JUCE 8, Direct2D emulated) | yes | complete, taps work; 12–14 s open, ≈2 cores while open |
| Surge XT (OpenGL) | yes, 2081 parameters | full editor, 2.2 s open; no factory patches without the installer's data folder |
| Surge XT Effects | yes | default skin (same data-folder caveat) |
| Pedal Faster, Windows build (JUCE 9, GDI) | yes | complete, 1.7 s open |
| E-Piano, Brass Knuckles, Pedal Faster (native builds) | yes, 40–140 µs per block | native editors |
| Odin2 (JUCE 7, software renderer) | yes | complete (Phase 4) |

## Not in the corpus yet

Installer-only downloads: OB-Xd, Odin2 (official), TAL plugins, Helm, Neural Amp Modeler,
Chow* plugins, u-he (Zebralette, Tyrell, Podolski), sfizz, Vital. They need the installer-based
import (run the installer under Wine silently, or extract it) that is on the list. Cardinal's
1.1 GB zip is excluded for size only.
