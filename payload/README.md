# Payload (Wine + Box64 + glibc)

Builds everything VST gHost runs under emulation. Binaries are never committed; `payload/out/` is git-ignored.

| Piece | Source (pinned in [versions.env](versions.env)) | Notes |
|---|---|---|
| Box64 0.4.4 | built from source, cross-compiled for aarch64 glibc, `-DSD8EG5=1 -DWINLATOR_GLIBC=1` | `PT_INTERP` = placeholder path, rewritten on the device |
| glibc 2.44 + libstdc++/libgcc_s | Termux-pacman prebuilt packages (Android-seccomp-patched), SHA-256-pinned | only the ~14 runtime libs are copied |
| Wine 11.0 | built from source: x86_64 only, headless (no X11/GL/Vulkan/audio/fonts) | patch: server dir inside `WINEPREFIX` (Android has no `/tmp`) |
| WINEPREFIX | created once at build time with `wineboot -i` | `.update-timestamp = disable`, only `c:` and `z:` |
| `tests/hello.exe` | [tests/hello.c](tests/hello.c), MinGW x86_64 | prints Wine/host/CPU info, runs [shared/bench](../shared/bench/bench_kernel.h) |
| `host/vstghost-host.exe` | [host/src](../host/src), MinGW x86_64, VST3 `pluginterfaces` 3.8.1 (MIT, pinned) | the bridge host (`scan` / `info` / `render`) |
| `tests/phase1.mid` | [tools/make_test_midi.py](../tools/make_test_midi.py) | Phase 1 test MIDI |
| Prefix setting | `HKCU\Software\Wine\Drivers\Graphics=null` | headless window creation (needed by JUCE) |

## Build

On Ubuntu 24.04 x86_64 (WSL2 or Docker):

```bash
cd "/mnt/c/Users/<you>/OneDrive/Documents/GitHub/VST Bridge"   # repo root
bash payload/setup-ubuntu.sh        # once: apt dependencies (sudo)
bash payload/build.sh               # all steps; or e.g. `bash payload/build.sh wine prefix pack play`
```

Build trees go to `~/vstghost-payload-work` and downloads to `~/.cache/vstghost-payload`, both on the Linux filesystem. Building Wine on `/mnt/c` is very slow. Output goes to `payload/out/`.

Docker alternative:
```bash
docker build -t vstghost-payload payload
docker run --rm -v "$PWD:/src" -w /src vstghost-payload bash payload/build.sh
```

## Layout inside the archive (= `files/rootfs` on the device)

```
VERSION, manifest.json
glibc/lib/          arm64 glibc (ld-linux-aarch64.so.1, libc.so.6, …)
box64/bin/box64     arm64; PT_INTERP → <root>/glibc/lib/ld-linux-aarch64.so.1
box64/x64lib/       x86_64 helper libs shipped by Box64
wine/               Wine 11.0 (bin/wine, bin/wineserver, lib/wine/x86_64-{unix,windows})
prefix/             WINEPREFIX
tests/hello.exe
etc/box64.box64rc
```

## Relocation

The payload is built for the 64-character prefix `/vstghost_root` + 50 underscores (`PLACEHOLDER` in build.sh, `Payload.PLACEHOLDER` in the app). When the app extracts the payload, it rewrites that prefix to the real `files/rootfs` path in ELF `PT_INTERP`, `#!` scripts and symlink targets. The real path must be ≤ 64 characters, which it is: `/data/user/0/com.vstghost.sideload/files/rootfs` is 46.

## Play edition extras (`out/jnilibs`, `out/play-assets`)

The Play build can't exec anything from `files/`. So `step_play` copies the loader, Box64 and the glibc libs into `jnilibs/arm64-v8a/` as `lib*.so` names (`libc.so.6` → `libc_so_6.so`) and writes `jnilibs.map`. The app rebuilds the sonames as symlinks and starts Box64 as `libldlinux.so --library-path <shim> libbox64.so …`. Gradle picks both folders up automatically for the `play` flavor.

## Licenses

Wine: LGPL-2.1+. Box64: MIT. glibc: LGPL-2.1+. libstdc++/libgcc_s: GPL-3 with the GCC runtime library exception. Their sources are the pinned URLs above. A release of the payload must ship or link those sources.
