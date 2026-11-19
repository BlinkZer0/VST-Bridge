# Plugin compatibility

Free Windows VST3 plugins run through VST gHost's Wine/Box64 path. The corpus is fetched by
`tools/corpus/fetch.sh` (manifest: `tools/corpus/corpus.txt`); `tools/corpus/pc-triage.sh` runs
each plugin on the PC with the phone's configuration (our Wine with the Wayland driver into our
compositor, llvmpipe OpenGL, the same patches and environment) and `tools/corpus/phone-triage.sh`
repeats the load, a self-test and an editor picture through the app on the phone.

_PC results: 2026-11-19, payload series 2 (Wine 11.0 + patches 0001–0004, Box64 0.4.4, Mesa 25.0.7)._
_Phone column: 2026-11-19, Galaxy S26 Ultra (Android 17), Play edition, same payload, through the app
(`tools/corpus/phone-triage.sh`): **all 30 import as zips, load and run with 0 underruns; the 25 plugins
with a GUI open their editors** (the five GUI-less ones fail as expected). Audio = the self-test peak: a
C-E-G chord for instruments, the engine's test signal through the FX chain for effects (Kars is hot by
design). Editor pictures: `C:/Users/Randy/vstghost-corpus/results-phone*/<plugin>.png` (not in git)._

**Audio:** 30 of 30 load and render (`×realtime` = how many times faster than real time the PC
rendered 20 s at 48 kHz, block 256). AmplitudeImposer and Stochas render silence by design (a
sidechain effect without a second input; a MIDI sequencer). **Editors:** 25 show a picture;
MaBitcrush/MaFreeverb/MaGigaverb/MaPitchshift and Kars ship without a GUI. Five hosts did not exit
within 90 s after the editor was closed (Dexed, ProM, glBars, DragonflyEarlyReflections, wolf-shaper)
— the editor itself had rendered; the close path is to be looked at.

| Plugin | Source | Kind | Params | Audio (peak dBFS, ×realtime) | Editor (PC) | Phone (Galaxy S26 Ultra) |
|---|---|---|---|---|---|---|
| ADLplug | adlplug | instrument | 2152 | -11.3, 680.9 | yes (32 ms) | -17.0 dBFS; editor 138 ms |
| BasicLimiter | Uhhyou | effect | 9 | -5.9, 747.6 | yes (189 ms) | -10.5 dBFS; editor 348 ms |
| CollidingCombSynth | Uhhyou | instrument | 74 | -15.6, 17.7 | yes (212 ms) | -19.6 dBFS; editor 382 ms |
| CubicPadSynth | Uhhyou | instrument | 1564 | -5.5, 28.9 | yes (202 ms) | -9.1 dBFS; editor 379 ms |
| Dexed | dexed | instrument | 2264 | -3.3, 383.1 | yes; host hung on close | -9.1 dBFS; editor 11.7 s |
| 3BandEQ | dpf-plugins | effect | 9 | -5.9, 1545.3 | yes (231 ms) | -10.5 dBFS; editor 1.4 s |
| 3BandSplitter | dpf-plugins | effect | 9 | -13.9, 1430.8 | yes (230 ms) | -24.9 dBFS; editor 1.4 s |
| AmplitudeImposer | dpf-plugins | effect | 5 | silent, 1505.5 | yes (142 ms) | silent by design (needs a sidechain); editor 1.3 s |
| CycleShifter | dpf-plugins | effect | 5 | -0.7, 2137.6 | yes (130 ms) | -5.2 dBFS; editor 1.3 s |
| Kars | dpf-plugins | instrument | 2083 | 8.3, 1526.9 | no GUI (plugin has none) | 4.5 dBFS; no GUI |
| MVerb | dpf-plugins | effect | 12 | -6.4, 338.2 | yes (204 ms) | -15.0 dBFS; editor 1.7 s |
| MaBitcrush | dpf-plugins | effect | 1 | -3.5, 1594.0 | no GUI (plugin has none) | -9.5 dBFS; no GUI |
| MaFreeverb | dpf-plugins | effect | 4 | -17.1, 599.6 | no GUI (plugin has none) | -23.6 dBFS; no GUI |
| MaGigaverb | dpf-plugins | effect | 8 | -2.5, 469.4 | no GUI (plugin has none) | -7.8 dBFS; no GUI |
| MaPitchshift | dpf-plugins | effect | 4 | -5.9, 161.8 | no GUI (plugin has none) | -10.5 dBFS; no GUI |
| Nekobi | dpf-plugins | instrument | 2090 | -17.8, 551.7 | yes (178 ms) | -18.5 dBFS; editor 1.4 s |
| PingPongPan | dpf-plugins | effect | 5 | -5.9, 525.8 | yes (213 ms) | -10.5 dBFS; editor 1.4 s |
| ProM | dpf-plugins | effect | 0 | -5.9, 2011.8 | yes; host hung on close | visualizer, passes audio; editor 2.2 s |
| SoulForce | dpf-plugins | effect | 7 | -0.8, 1002.3 | yes (120 ms) | -0.8 dBFS; editor 1.3 s |
| glBars | dpf-plugins | effect | 5 | -5.9, 2332.1 | yes; host hung on close | visualizer, passes audio; editor 1.3 s |
| DragonflyEarlyReflections | dragonfly | effect | 9 | -7.6, 506.0 | yes; host hung on close | -11.6 dBFS; editor 1.6 s |
| DragonflyHallReverb | dragonfly | effect | 20 | -7.3, 85.5 | yes (174 ms) | -12.4 dBFS; editor 1.6 s |
| DragonflyPlateReverb | dragonfly | effect | 11 | -7.9, 585.1 | yes (173 ms) | -12.4 dBFS; editor 1.6 s |
| DragonflyRoomReverb | dragonfly | effect | 19 | -7.9, 89.6 | yes (191 ms) | -12.4 dBFS; editor 1.6 s |
| MoniqueMonosynth | monique | instrument | 2434 | -3.3, 26.3 | yes (465 ms) | -4.9 dBFS; editor 2.0 s |
| OPNplug | opnplug | instrument | 2152 | -13.5, 162.6 | yes (31 ms) | -19.2 dBFS; editor 137 ms |
| Stochas | stochas | instrument | 2137 | silent, 7189.1 | yes (492 ms) | silent by design (sequencer); editor 2.0 s |
| Surge XT Effects | surge-xt | effect | 14 | -7.4, 959.2 | yes (479 ms) | -15.6 dBFS; editor 2.0 s |
| Surge XT | surge-xt | instrument | 2855 | 0.3, 166.8 | yes (710 ms) | -3.1 dBFS; editor 2.2 s |
| wolf-shaper | wolf-shaper | effect | 13 | -5.9, 561.7 | yes; host hung on close | -10.5 dBFS; editor 2.6 s |

## Verified on the phone so far (Galaxy S26 Ultra, Play edition)

| Plugin | Audio | Editor |
|---|---|---|
| Dexed (JUCE 8, Direct2D emulated) | yes | complete, taps work; 12–14 s open, ≈2 cores while open |
| Surge XT (OpenGL) | yes, 2081 parameters | full editor, 2.2 s open; no factory patches without the installer's data folder |
| Surge XT Effects | yes | default skin (same data-folder caveat) |
| Pedal Faster, Windows build (JUCE 9, GDI) | yes | complete, 1.7 s open |
| No Strings, Windows build (JUCE 8.0.17) | yes, 55 parameters, 0.6 s load | blank: the editor is a WebView2 (Edge/Chromium) web UI, which the Wine prefix cannot provide — use the native build |
| The Ham, Windows build (JUCE 8.0.17) | yes, 145 parameters, 0.6 s load | blank (WebView2 web UI, as above) |
| No Strings, The Ham (native builds) | yes, 4–8 ms load | complete web UI editors (Android WebView), presets switch from the UI |
| E-Piano, Brass Knuckles, Pedal Faster (native builds) | yes, 40–140 µs per block | native editors |
| Odin2 (JUCE 7, software renderer) | yes | complete (Phase 4) |

## Not in the corpus yet

Installer-only downloads: OB-Xd, Odin2 (official), TAL plugins, Helm, Neural Amp Modeler,
Chow* plugins, u-he (Zebralette, Tyrell, Podolski), sfizz, Vital. They need the installer-based
import (run the installer under Wine silently, or extract it) that is on the list. Cardinal's
1.1 GB zip is excluded for size only.
