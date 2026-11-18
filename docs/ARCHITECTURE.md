# Architecture

Status: **proposal for review, no code yet.** It covers the decisions on the three hard constraints (exec, real-time audio, AAP registration) and the shape of everything else. Facts are sourced in [PRIOR_ART.md](PRIOR_ART.md). Anything marked *assumption* has to be proven on the S26 Ultra before we rely on it.

## 1. Overall shape

```
 Other apps ─── AAP host (Binder) ──┐      ┌── MIDI apps (android.media.midi)
                                    ▼      ▼
┌──────────────── APK process(es): arm64, bionic ─────────────────────────────┐
│ Kotlin UI · AudioPluginService (slots) · MidiDeviceService (slots)          │
│ C++ engine: Oboe stream, MIDI in, slot table, host supervisor, metering     │
│      │ memfd shared mem (audio + MIDI rings, control word)                  │
│      │ AF_UNIX socket (control RPC + passing the memfd via SCM_RIGHTS)      │
└──────┼──────────────────────────────────────────────────────────────────────┘
       │ fork/exec, one host per plugin instance
┌──────▼──────────── bridge host: x86_64, under Box64 + Wine ─────────────────┐
│ vstbridge-host (winelib: Unix-side IPC + PE-side plugin loading)            │
│   render thread: futex wait → plugin process() → write output ring         │
│   main thread:   control RPC, Win32 message pump (no display driver)        │
│                          │ VST3 (VST2 later)                                │
│                    plugin.vst3 / plugin.dll                                 │
└─────────────────────────────────────────────────────────────────────────────┘
 Also running: wineserver (x86_64 under Box64), one per WINEPREFIX
```

Choices that apply across the design:

- **arm64-v8a only**, Kotlin + C++20 (NDK, CMake), Oboe.
- **Headless Wine through Phase 3.** No X server, no PulseAudio: audio never goes through Wine's audio stack. The prefix sets `HKCU\Software\Wine\Drivers\Graphics=null`, so Wine uses its **null graphics driver**: invisible windows and message loops work. Without that setting, Wine's no-driver path refuses every non-message window, including JUCE's hidden message window. **Proven in Phase 1:** 4/4 JUCE plugins load and process headless.
- **64-bit Windows plugins only in v1.** No WoW64 and no Box86. 32-bit plugins are reported as unsupported.
- **One host process per plugin instance** (yabridge's default), so a plugin crash takes down only its own slot. Grouping several plugins in one host can come later if memory use requires it.
- **Bridge host: a plain x86_64 Windows exe built with MinGW (decided in Phase 1, revised from "winelib").** Phase 1 needs no Linux syscalls, and a PE exe is independent of Wine's build internals: yabridge's winelib host has repeatedly broken on new Wine versions. The VST3 hosting core (`host/src/vst3host.*`) is kept separate from transport. **Decided in Phase 2:**
  - **Transport:** a file-backed shared mapping (`files/ipc/slotN.shm`): the engine `mmap`s it, and the host uses `CreateFileMapping`, which Wine maps `MAP_SHARED`.
  - **Wakeups:** Linux futexes on words in that mapping. The PE host issues them as a raw x86_64 `syscall`. Wine x86_64 doesn't enable syscall user dispatch, and Box64 translates the call.
  - **Control:** a request/response mailbox in the same mapping.
  - **Ring indices:** `seq_cst` atomics. Verified: 0 mismatches in 1.9 M samples without `STRONGMEM`.

## 2. Constraint 1: exec restrictions (W^X), and the two editions

### Two editions (decided 2026-11-18)

| | **Sideload edition** (`com.vstghost.sideload`) | **Play edition** (`com.vstghost`) |
|---|---|---|
| targetSdk | 28 | 36 (Play requirement) |
| Wine/Box64 payload | imported from a local file (adb or Files); later a pinned, verified download is possible | **shipped inside the app and updated only through Play.** Nothing executable is downloaded at runtime. |
| arm64 native code (Box64, glibc) | extracted to `filesDir` and exec'd there | packaged as `lib*.so` in `jniLibs` → `nativeLibraryDir` (the only executable location for targetSdk ≥ 29) |
| x86_64 code (Wine, host, plugins) | data in `filesDir` | data in `filesDir`, extracted from the app's own assets (later a Play install-time asset pack). Only Box64 interprets or translates it, which is the emulator-style exception. |
| Loading the user's own Windows VSTs | yes | offered as an emulator-style feature. If Play review rejects it, the Play build ships without it. |
| Native AAP plugins (open-source ports plus e-piano, Brass Knuckles, The Ham) | yes | yes; this pulls part of Phase 5 forward for the Play edition |

Both editions are Gradle product flavors built from one codebase. The only difference is how the payload is placed and launched (`PayloadLayout` in the app). The policy reasoning comes from you: Play's rule against downloading executable code has an exception for code running in a VM or interpreter, and GameHub is a Wine + Box64 app that got onto Play. I haven't independently verified the GameHub precedent.

### Sideload edition: `targetSdkVersion 28`, payload extracted to `filesDir` and executed from there.

This is what Winlator 11.2 does today (Aug 2026, `compileSdk 35 / targetSdk 28`), and Android 16 still installs apps that target API 24 or higher. We are not depending on an unproven trick, and this edition is never submitted to Play. It's also the fallback if the Play edition's tricks hit a wall.

Details we have to get right (Phase 0 checks each one):

1. **Payload delivery.** The app doesn't ship the payload in the APK. A setup screen downloads a pinned, SHA-256-verified archive (Box64, glibc rootfs, Wine, a pre-built WINEPREFIX), or imports it from a local file when testing over adb. That keeps the APK small and the binaries out of git. The payload is built by scripts in `payload/` (§7).
2. **Relocation.** glibc binaries hard-code the dynamic loader path (`PT_INTERP`), and Termux/Winlator rootfs builds bake their own package path into it. We build the rootfs with a **long placeholder prefix** and, at extraction, rewrite `PT_INTERP` and the few hard-coded paths to the real `filesDir` path (the same technique conda uses). The fallback is to start everything through the loader explicitly: `ld-linux-aarch64.so.1 --library-path … box64 …`. We prefer the rewrite because Box64 and Wine re-exec themselves (wineserver, `wine` → `wine64`), and `/proc/self/exe` pointing at the loader can break that. *Phase 0 tests this.*
3. **Android seccomp.** App processes run under a seccomp filter that kills processes calling blocked syscalls. Stock glibc (which uses `clone3`, `rseq` and others) needs the Termux/Winlator glibc patches, so we build glibc from those patches and don't use Debian's packages.
4. **Process survival.** Android 12+ has a "phantom process" killer for child processes (limit about 32 system-wide, plus kills for CPU use in the background). A Wine session is wineserver + host + `services.exe` + others, about 4–6 processes. Mitigations: a foreground service (`mediaPlayback`) while any host runs, a minimal prefix (disable `winemenubuilder`, `explorer` where possible), and for development, the documented adb setting to lift the limit (exact commands go in the test steps).

### Play edition: `targetSdk 36` with executables in `jniLibs` (required, no longer just a probe)

Under Box64, the only native arm64 code is Box64 itself plus the arm64 glibc (`ld-linux`, `libc`, `libm`, `libpthread`, `librt`, `libdl`, `libstdc++`, `libgcc_s`). Every x86_64 file (Wine, the host, the plugins) is just *data* that Box64 reads and translates into anonymous JIT memory, which apps may still map executable. So:

- Those arm64 files ship as `lib*.so` in `jniLibs` with `useLegacyPackaging = true`, so they're extracted to `nativeLibraryDir`, which is executable but read-only and lives at an unpredictable path.
- **Proven in Phase 0 (Android 17, S26 Ultra):** `libbox64.so`'s `PT_INTERP` is set at build time to `/data/data/com.vstghost/files/plibs/ld-linux-aarch64.so.1`. That fixed path is a symlink the app creates pointing to `nativeLibraryDir/libldlinux.so`, and the glibc sonames are symlinked the same way. SELinux evaluates the symlink *target* (an APK lib), so exec and `mmap(PROT_EXEC)` are allowed. Box64 can exec and re-exec itself directly (`wine` → `wineserver`, services), with no Box64 patch. The first attempt (an explicit `libldlinux.so … libbox64.so` launch) failed exactly at Box64's self re-exec.
- Box64 does not need executable mappings of the x86 files in `files/`: 0 `avc` denials.
- Limitation: `/data/data/<pkg>` is the primary user's path. Secondary users and work profiles need `/data/user/<n>/…`.
- `BOX64_DYNACACHE=0` (Winlator does the same), so Box64 never maps cached code from disk.

## 3. Constraint 2: real-time audio across the emulation boundary

### Decision: an asynchronous, pipelined render-ahead design. The Oboe callback never waits on the bridge host.

yabridge's audio call is synchronous (the DAW thread blocks on the socket round-trip). That's acceptable on desktop Linux, but here the other side is a non-real-time process going through Box64 and Wine, so we decouple the two sides with a configurable amount of buffering:

```
Oboe callback (SCHED_FIFO, arm64)                    Host render thread (x86_64 under Box64)
  1. push MIDI events (sample-timestamped) ─► [MIDI ring, SPSC]  ─► read events for block n
  2. pop rendered frames                    ◄─ [audio out ring, SPSC] ◄─ write block n
  3. if ring short → output silence, xrun++     (effects: [audio in ring] in the other direction)
  4. futex_wake(control word) ─────────────────► futex_wait wakes; renders until the ring
                                                  holds D blocks
```

- **All IPC memory is one `memfd`**, created on the Android side and passed to the host over the AF_UNIX control socket with `SCM_RIGHTS`, then mapped by both sides. It holds the audio rings (float32, non-interleaved, one per bus), a MIDI event ring, a transport/tempo block, and the counters the measurement screen reads.
- **Wakeup** uses a cross-process (non-private) `futex` on a word in the shared mapping. `FUTEX_WAKE` from the Oboe callback is a non-blocking syscall. The callback takes no locks, allocates nothing, and does no socket I/O.
- **Plugin block size** (B_p) is fixed per session (64/128/256/512) and independent of Oboe's callback size; the rings absorb the difference.
- **Prefill depth D** (the "extra buffer") is user-configurable. Added latency = D × B_p frames on top of Oboe's own output latency. The engine also adapts: after repeated underruns it raises D by one block, with an option to lock it.
- **Underrun policy:** output silence for that callback, count an xrun, and let the host catch up. Never block.
- **Effects** add D × B_p of latency, reported to hosts through the AAP latency information where AAP supports it. Instruments only add MIDI-to-sound latency.

**⚠ Memory ordering under Box64.** x86 code doesn't need barriers for acquire/release because x86 is TSO, so the compiler emits plain `mov`s. Box64 translates those into ordinary arm64 loads and stores, which **can reorder**. Our host's ring index accesses would be silently unsafe. Two layers of mitigation: (a) in the x86 host, use `seq_cst` atomics for ring indices (these compile to `xchg`/`lock` ops, which Box64 translates with barriers); (b) give the host binary `BOX64_DYNAREC_STRONGMEM=1` in a per-exe section of the box64rc. Phase 2 includes a ring-buffer stress test across the boundary to confirm. This is the riskiest subtle bug in the design.

**Thread scheduling for the host process.** It can't get `SCHED_FIFO`. *Assumption, to be measured:* we can (a) lower nice on the render thread with `setpriority`, depending on `RLIMIT_NICE` for the app uid, and (b) pin it to the performance cores with `sched_setaffinity` within the app's cpuset. Both need the app to be foreground (activity or foreground service) so its cpuset isn't restricted to the little cores. We'll measure what's actually allowed on the S26 Ultra and record it in STATUS.md.

**Box64 settings for the host.** We start from Box64's defaults, add `STRONGMEM=1`, and set `BOX64_AVX` per plugin if a plugin's AVX code path misbehaves. Each setting gets a measured A/B comparison (CPU % at a fixed load); we won't rely on folklore.

**Measurement (built into the app from Phase 2):**
- Output latency from Oboe/AAudio `getTimestamp()`, plus the known D × B_p.
- **Round-trip latency measured physically**: an impulse through a loopback (headset adapter with a cable from out to in, or speaker to mic) using the same method as OboeTester, giving MIDI-in → audio-out end to end. The test plan says which method was used, since speaker/mic includes acoustic delay.
- xrun count (ours and AAudio's `getXRunCount()`), host render time per block (p50/p99/max), host CPU % from `/proc/<pid>/stat`, ring fill level over time.
- Recorded for B_p = 128/256/512 at 48 kHz, as the brief asks.

**Crash isolation.** The supervisor (in the app's native engine) watches each host through `waitpid` and socket EOF, with a watchdog on how long a render can stall. When a host dies, its slot outputs silence, the UI shows "plugin crashed" plus the last stderr lines from Wine/Box64, and the user can restart it with its last saved state. The Oboe stream and the other slots keep running.

## 4. Constraint 3: exposing plugins to other apps

### Facts
- AAP plugin discovery is **static**. Hosts find services by intent action and read `@xml/aap_metadata` from the manifest meta-data. The plugin list is fixed when the APK is built, and nothing in aap-core registers plugins at runtime.
- AAP **parameters can be dynamic**: the parameters extension has `get_parameter_count` / `get_parameter`, plus `notify_parameters_changed` to make the host query again.
- Android virtual MIDI devices (`MidiDeviceService` / `MidiUmpDeviceService`) are also static: one manifest service class per device.

### Decision: a fixed set of **bridge slots**, shared by AAP and virtual MIDI.

- The APK declares, by default, **4 instrument slots** and **4 effect slots**, named e.g. "VST Bridge Inst 1" … "VST Bridge FX 4". The count is a Gradle property that generates the XML and the manifest entries.
- The user assigns any bridged VST to a slot in our app. The slot's AAP name is fixed (it's static), but:
  - its **parameter list comes from the loaded plugin** through the AAP parameters extension at runtime, and `notify_parameters_changed` fires when the assignment changes;
  - its **state** (AAP state extension) stores which VST is assigned plus that plugin's own state chunk, so a host project reopens with the right plugin loaded.
- Each instrument slot is also a **virtual MIDI device** (one `MidiDeviceService` subclass per slot, generated code). With UMP (`MidiUmpDeviceService`, API 35+) as an option, following AAP 0.9's approach. A MIDI app plays the slot; audio comes out through our own Oboe stream.
- Slots share the same engine and host processes as the standalone app, so a VST that is open in our app and also assigned to slot 2 is one plugin instance, not two.
- *Assumption, verified in Phase 3:* `aap-juce-simple-host` handles plugins that declare no parameters in XML and supply them at runtime. If it doesn't, the fallback is to declare a fixed block of generic parameters per slot (e.g. P1–P128) and map them to the plugin's real parameters.

The AAP `process()` path deserves its own note. An AAP host calls us over synchronous Binder with no priority inheritance, and our service then forwards to the Wine host. The same render-ahead ring applies, so the slot never blocks on the Wine host for longer than its prefill. Phase 3 measures the real numbers; we expect AAP-hosted latency to be worse than our standalone app.

## 5. Plugin UI before Phase 4

Generic parameter sliders built from the plugin's parameter list (VST3 `IEditController::getParameterInfo`), with the plugin's own value-to-string formatting, search, and preset load/save (VST3 `.vstpreset` / state chunks). There's no X server until Phase 4. The Phase 4 approach follows Winlator: an X server inside the app, one Activity per editor, and touch-to-mouse mapping.

## 6. Copy protection and failure reporting

iLok, PACE and online activation are out of scope. The host classifies load failures as best it can: missing DLL (Wine's `err:module` output), an unimplemented Win32 function, a crash during `GetPluginFactory`, a timeout, or a known PACE/iLok module present in the bundle. It reports a clear reason per plugin in the compatibility table rather than a generic "failed".

## 7. Payload and build

Payload contents (pinned versions, SHA-256 in `payload/manifest.json`, built reproducibly in Docker, never committed):

| Component | Source | Notes |
|---|---|---|
| Box64 | built from source at a pinned tag (MIT) | aarch64 glibc build, Android-friendly flags |
| arm64 glibc | Termux-pacman `glibc-2.44-0` + `gcc-libs-glibc-14.2.1-1` **prebuilt packages**, pinned by SHA-256 (LGPL/GPL+exceptions) | already patched for Android seccomp. *Deviation:* not rebuilt from source in Phase 0; rebuilding from termux-packages recipes is tracked in STATUS. Pacman repos drop old versions, so we mirror the exact files with our releases. |
| x86_64 runtime libs | glibc and the libs Wine needs (x86_64) | Box64 emulates or wraps them |
| Wine | built from source, x86_64 only, a 10.x version close to Winlator's 10.10 | no X11, PulseAudio or GStreamer for Phases 0–3 |
| WINEPREFIX | created once at build time (`wineboot`) | avoids a slow first-run `wineboot` on the phone |
| vstbridge-host | our winelib build, x86_64 | built with the same toolchain as Wine |

Gradle builds only the APK (Kotlin + NDK). `payload/build.sh` produces `vstbridge-payload-<ver>.tar.zst`. For development it's pushed with `adb push` and imported; later it can be downloaded from a release URL.

**Build environment:** the payload needs Linux (Docker Desktop or WSL2 on your Windows machine). The APK builds on Windows with Android Studio / Gradle as usual.

Proposed repo layout:

```
app/            Kotlin UI, services, generated slot manifests
engine/         C++ NDK: Oboe, MIDI, IPC (Android side), supervisor, meters
host/           C++ winelib bridge host (x86_64)
shared/         IPC layout headers shared by engine and host (plain C structs, static_asserts on layout)
payload/        Dockerfile + scripts that build the Wine/Box64/rootfs payload
tools/          desktop test helpers (MIDI → WAV renderer, latency analysis)
docs/           ARCHITECTURE.md, PRIOR_ART.md, per-phase test steps
STATUS.md
```

## 8. Phase 0 plan

1. One codebase with two flavors: `sideload` (targetSdk 28) and `play` (targetSdk 36), arm64 only, and no AndroidX or other dependencies beyond the Kotlin stdlib.
2. A native extractor (tar + zstd, in C++) that restores modes and symlinks and rewrites `PT_INTERP` from the placeholder prefix (sideload). The play flavor additionally builds the symlink shim to `nativeLibraryDir`.
3. A launcher that runs a command, streams stdout/stderr into the UI and logcat (tag `VSTGHOST`), and samples `/proc` for every child process of our uid (process count, summed RSS, peak VmHWM).
4. Payload v0: Box64 (`-DSD8EG5`), glibc, Wine 11.0 (x86_64 only, headless), a pre-built prefix, and `hello.exe`. hello.exe prints the Wine version, the host kernel and the CPU string, then runs a fixed DSP benchmark (16 saw voices + filters, 10 s at 48 kHz). The app runs the **same benchmark natively in arm64**, which gives a first Box64 overhead factor.
5. Measurements, all on the device: extraction time, payload size (compressed and on disk), `box64 --version`, cold `wine hello.exe` (no wineserver running), warm runs (wineserver up), peak memory, process count (relevant to the phantom-process limit), and native-vs-Box64 benchmark time.
6. An adb-driven autorun (`am start … --ez autorun true`) that writes `files/phase0-report.md`, so you can run the whole thing from a PC and pull the report back.
7. Deliverable: APKs for both editions, the report, and exact adb steps in `docs/phase0-test-steps.md`.

## Decisions recorded (2026-11-18)

1. **License: GPL-3.0-or-later** (allows reusing yabridge code). `LICENSE` added.
2. **Payload is built from source** (Box64, Wine) in a Linux build environment, with an Android-patched arm64 glibc. See §7 for the one deviation: Termux's prebuilt glibc package, pinned by SHA-256.
3. **AGPL (via aap-juce) is acceptable** for the user's JUCE plugins.
4. **App name "VST gHost"**, package `com.vstghost` (Play edition) and `com.vstghost.sideload` (sideload edition).
5. **4 instrument + 4 effect slots** by default.
6. **Two editions**, Play and sideload (§2).
