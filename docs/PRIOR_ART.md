# Prior art: licenses and status

Checked on **2026-11-18** against each project's GitHub repo (API `license.spdx_id`, `pushed_at`, latest release) and, where noted, the license file itself. Re-check before each phase that pulls in new code.

| Project | What we take from it | License (verified) | Status (2026-11-18) |
|---|---|---|---|
| [yabridge](https://github.com/robbert-vdh/yabridge) | Architecture; possibly code (VST3 proxy/serialization, `audio-shm`, VeSTige VST2 headers) | **GPL-3.0** | Active. Last push 2026-11-08. Latest release 5.1.1 (2024-11-04); master has unreleased fixes, including Wine ≥ 9.22 GUI mouse input. |
| [Winlator](https://github.com/brunodev85/winlator) + [winlator-app](https://github.com/brunodev85/winlator-app) (the app source is a submodule) | How to package Wine + Box64 + a glibc rootfs in an APK; glibc patches | **LGPL-2.1** | Active. v11.2.0 released 2026-08-19, last push 2026-11-02. |
| [Box64](https://github.com/ptitSeb/box64) | x86_64 → arm64 translation | **MIT** | Very active (pushed today). Latest release v0.4.4 (2026-08-02). |
| [aap-core](https://github.com/atsushieno/aap-core) | Android plugin format we expose | **MIT** (LICENSE file read) | Very active. README says the API is **not stable yet**, so pin a commit. |
| [aap-juce](https://github.com/atsushieno/aap-juce) | Reference host (`aap-juce-simple-host`); Phase 5 JUCE → AAP | **AGPL-3.0** ⚠ | Active. |
| [UAPMD](https://github.com/atsushieno/uapmd) | How plugins become virtual MIDI 2.0 devices | **MIT** (`uapmd-ara` and `uapmd-mir` are Apache-2.0) | Active, v0.5.x. On Android it relies on AAP for MIDI devices, and its README marks Android virtual MIDI 2.0 devices as "missing (AAP supports it statically)". |
| [Oboe](https://github.com/google/oboe) | Low-latency audio I/O | **Apache-2.0** | Active. Latest release 1.11.0 (2026-09-15). |
| [VST3 SDK](https://github.com/steinbergmedia/vst3sdk) | Bridge host plugin API | **MIT**. Since 3.8 the GPLv3 and proprietary dual licenses are "no longer available." | 3.8.x. Last push 2026-08-11. |
| VST2 SDK | none | Discontinued by Steinberg and not redistributable | We would use yabridge's reverse-engineered VeSTige headers (`src/include/vestige/aeffectx.h`) under its GPL. |
| [CLAP](https://github.com/free-audio/clap) | Phase 5 native fast path | **MIT** | Active. |
| [JUCE](https://github.com/juce-framework/JUCE) | The user's own plugins (Phase 5) | **AGPLv3 or commercial** (JUCE 9 licence) | Active. |
| Wine | Windows API layer | LGPL-2.1+ | Winlator 11.2 ships Wine **10.10**. |
| Box86 | Not used (64-bit only in v1) | MIT | n/a |
| Hangover | Not used (alternative to Box64 + Wine) | LGPL-2.1 | n/a |

## License consequences

- **If we reuse any yabridge code, the whole app must be GPL-3.0.** Everything else above (MIT, Apache-2.0, LGPL-2.1) is compatible with GPL-3.0. The VST3 SDK's move to MIT removes what used to be the hard part.
- **aap-juce is AGPL-3.0.** The bridge app doesn't need it: aap-core is MIT, and aap-juce is only the test host. It matters in **Phase 5**: building your JUCE plugins as AAP plugins through aap-juce makes those builds AGPL unless you get other terms from its author. A commercial JUCE license covers JUCE, not aap-juce.
- No license file exists in this repo yet. Choosing one is your decision (see the open questions in [ARCHITECTURE.md](ARCHITECTURE.md#open-questions-for-you)).

## Facts from the source that drive the decisions

- **Winlator exec model** (`winlator-app/app/build.gradle`, `AndroidManifest.xml`, `GuestProgramLauncherComponent.java`): `compileSdk 35`, **`targetSdkVersion 28`**, `minSdk 26`, `abiFilters 'arm64-v8a'`, `extractNativeLibs="true"`. The payload ships as `.tzst` assets (`rootfs.tzst` is about 65 MB, plus `box64-0.4.4.tzst`), which are extracted into the app's files dir. Box64 is then exec'd directly from `rootfs/usr/local/bin/box64` with `BOX64_DYNACACHE=0`, a `BOX64_RCFILE`, and `ANDROID_SYSVSHM_SERVER`, because Android has no SysV shm and Winlator emulates it for X11. Exec from the data dir works only because of targetSdk 28.
- **Android install floor**: Android 14 and later refuse to install apps targeting API < 24 (`INSTALL_FAILED_DEPRECATED_SDK_VERSION`), and Android 16 kept that floor at 24 ([Bayton matrix](https://bayton.org/android/android-minimum-targetsdk-matrix/)). targetSdk 28 therefore still installs on Android 16. Google Play requires targetSdk 36, so the app is sideload-only.
- **AAP discovery** (`docs/PLUGIN_SCRATCH_GUIDE.md`): hosts query services with the intent action `org.androidaudioplugin.AudioPluginService.V4` and read the meta-data `org.androidaudioplugin.AudioPluginService.V4#Plugins` → `@xml/aap_metadata`. This is static: the plugin list is fixed when the APK is built.
- **AAP parameters** (`include/aap/ext/parameters.h`): plugins can report parameters **at runtime** through `get_parameter_count()` / `get_parameter(index)`. The header says this is for when "the plugin does not provide the parameter list on aap_metadata". `notify_parameters_changed()` tells the host to query again.
- **AAP realtime** (`docs/realtime-processing.md`): host→plugin `process()` is a synchronous Binder transaction with **no realtime priority inheritance**.
- **Android virtual MIDI**: `MidiDeviceService` and `MidiUmpDeviceService` (API 35+) devices are also declared in the manifest, each with an XML device-info resource. Each device needs its own service class, so there is no runtime device creation.
- **yabridge IPC** (`docs/architecture.md`): Unix domain sockets with bitsery serialization. A second connection is opened ad hoc whenever a socket is busy. Since 3.4.0, audio buffers live in one shared-memory block (`AudioShmBuffer`), and the socket round-trip itself provides synchronization. One host process per plugin by default; plugin groups are optional. yabridge's audio path is **synchronous**: the DAW thread blocks until the Wine side returns.
- **Box64 tunables** (`docs/USAGE.md`): `BOX64_DYNAREC_STRONGMEM` (0–4, emulates x86 TSO), `BIGBLOCK` (default 2), `CALLRET` (default 2), `SAFEFLAGS`, `FASTNAN`/`FASTROUND` (default fast), `X87DOUBLE`, `BOX64_AVX` (default 2 on arm64, which advertises AVX2/FMA). Per-executable sections can go in a `BOX64_RCFILE`.
