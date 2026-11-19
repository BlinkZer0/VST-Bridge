# Third-party notices

VST gHost is proprietary software, © 2026 Blink Zero. It includes the open-source components below.
Each keeps its own license; the full texts are in this folder and shown in the app under ⋮ → About.
The bundled instruments and effect (E-Piano, Brass Knuckles, Pedal Faster, No Strings, The Ham) are
the app author's own work and part of the app.

| Component | What it is | License | Text |
|---|---|---|---|
| Wine 11.0 | runs Windows VST3 plugins (with VST gHost's patches) | LGPL-2.1-or-later | LGPL-2.1.txt, wine-LICENSE.txt |
| Box64 0.4.4 | runs x86-64 code on the phone's arm64 CPU | MIT | box64-MIT.txt |
| glibc 2.44 (Termux-pacman build) | C runtime for Wine and Box64 | LGPL-2.1-or-later | LGPL-2.1.txt |
| libstdc++, libgcc_s, libatomic (GCC 14) | C++ runtime for Wine and Box64 | GPL-3 with the GCC Runtime Library Exception | GPL-3.0.txt |
| Mesa 25.0.7 (llvmpipe) | software OpenGL / Direct3D for plugin editors | MIT | mesa-license.txt |
| VST3 SDK pluginterfaces 3.8.1 (Steinberg) | plugin interface headers used by the bridge host | MIT (VST 3 SDK, 3.8 and later) | — |
| JUCE 9.0.5 | framework of the bundled instruments | JUCE commercial license (see below) | JUCE-LICENSE.md |
| Oboe (Google) | Android low-latency audio | Apache-2.0 | — |
| Android Audio Plugin (AAP) | plugin hosting on Android | MIT | — |
| AndroidX, Material Components (Google) | user interface | Apache-2.0 | — |

## Source offer (LGPL components)

Wine and glibc are licensed under the GNU Lesser General Public License 2.1. They run as separate
programs inside the app's private storage and are not linked into the app. Their complete source
code, including VST gHost's modifications to Wine, is available on request from the contact address
on the app's Play Store listing, for at least three years after the app's release, at no more than
the cost of distribution. The unmodified sources are also published at winehq.org and gnu.org, and
the Termux-pacman glibc recipe at github.com/termux-pacman/glibc-packages.

## JUCE

The bundled instruments are built with JUCE under its commercial license terms (JUCE Personal /
Indie / Pro as applicable). JUCE is © Raw Material Software Limited.

## Plugins you install

Plugins you install yourself are not part of the app and come under their own licenses.
