# Phase 0 report: Wine + Box64 inside our own APK

**Result: both editions work on the target phone.** An x86_64 Windows console program runs under Wine 11.0 + Box64 0.4.4, launched from inside our APK. That includes the Play edition at targetSdk 36, with no SELinux denials. Its output is captured in the app.

All numbers were measured on the device on 2026-11-18, driven over adb with the app's autorun. Full device reports: [results/phase0-report-play.md](results/phase0-report-play.md) and [results/phase0-report-sideload.md](results/phase0-report-sideload.md).

## Device

Samsung Galaxy S26 Ultra **SM-S948U1**, SoC **SM8850** (Snapdragon 8 Elite Gen 5), **Android 17 (API 37)**, patch 2026-10-05, kernel 6.12.69-android16. The brief said Android 16; the phone is already on 17. RAM: 11.1 GB visible, about 3.2 GB available when the tests started.

## Numbers

| Metric | Play (`com.vstghost`, targetSdk 36) | Sideload (`…sideload`, targetSdk 28) |
|---|---|---|
| Payload archive (tar.zst) | 63.2 MB, bundled in the APK (APK 75.6 MB) | 63.2 MB, pushed to Android/data (APK 2.8 MB) |
| Extracted on disk | 407.5 MB, 2294 files | same |
| Extraction (first launch) | **1.95 s** | **1.94 s** |
| `box64 --version` | 3 ms | 3 ms |
| `wine --version` | 19 ms | 16 ms |
| **Cold start**: new Wine session → first line from hello.exe | **1074 ms** (exit at 1183 ms) | **1087 ms** (exit at 1192 ms) |
| **In-session start**: Wine session running → first line | **93–104 ms** (exit at 200–212 ms) | **104–139 ms** (exit at 211–248 ms) |
| Guest processes in a session | 7–8 | 7–8 |
| Peak summed RSS of guest processes | 440 MB at cold start, 380 MB in-session | 445 / 382 MB |
| hello.exe process alone (VmHWM) | ≈ 46 MB | ≈ 46 MB |
| DSP kernel, native arm64 (best of 3) | 82.0 ms | 82.4 ms |
| DSP kernel, x86_64 under Box64 (best of 4) | 97.1 ms | 96.7 ms |
| **Box64 overhead factor** (this kernel) | **1.18×** | **1.17×** |
| Float checksum native = Box64 = x86_64 PC | ✅ identical (`-407.86425463249907`) | ✅ identical |
| SELinux `avc` denials / phantom-process kills | 0 / 0 | 0 / 0 |

## What these numbers mean

- **Start-up is dominated by Wine's session services, not Box64.** The first Wine process in a session starts `wineboot --init`, `services.exe`, two `winedevice.exe`, `plugplay.exe` and `svchost.exe`. That takes about 1.1 s. Starting `wineserver -p` alone doesn't help: "warm run 1" still took about 1.3 s. Once the session is up, a new Windows process starts in about 0.1 s. → **Phase 2 design consequence:** keep one Wine session alive while the app or its plugin service is active, and start plugin hosts inside it. Each host then costs about 0.1 s plus plugin load time.
- **Memory:** summed RSS overstates real use, because shared pages (Box64's 30 MB binary, Wine's libraries) are counted once per process. PSS is the right metric and gets added in Phase 1. Even so, a session costs on the order of 300–400 MB before any plugin loads. The service processes (≈ 53–87 MB each) are candidates for trimming: disable unneeded services such as `winebth` and `plugplay` in the prefix.
- **Process count:** 7–8 per session. That's within Android's phantom-process limit (32 system-wide), but one shared session matters more than one per plugin.
- **Box64 overhead of 1.18× applies only to this kernel:** 32 voices of scalar float math. Real plugins use SSE/AVX; Box64 advertises AVX2 and translates it to NEON. Phase 1 measures real plugins. Neither test pins its thread to a core, and the first native run of each set was 102–112 ms against 82 ms afterwards, which is CPU frequency/core migration, not Box64.
- **Floating-point results are bit-identical** across native arm64, x86_64 under Box64 on the phone, and real x86_64 on the PC, for this code.

## Constraint 1: how the exec problem was solved

| | Sideload (targetSdk 28) | Play (targetSdk 36) |
|---|---|---|
| Where arm64 binaries run from | `files/rootfs` (allowed below API 29) | `nativeLibraryDir` (shipped as `lib*.so`) |
| How Box64 finds glibc's loader | `PT_INTERP` rewritten at extraction from the 64-char build placeholder to `files/rootfs/glibc/lib/ld-linux-aarch64.so.1` | `PT_INTERP` set at build time to **`/data/data/com.vstghost/files/plibs/ld-linux-aarch64.so.1`**, a symlink the app creates pointing into `nativeLibraryDir`. SELinux evaluates the target (an APK lib), so the exec is allowed. |
| x86_64 code (Wine, hello.exe) | data in `files/`, translated by Box64 | same; no denials, so Box64 doesn't need `PROT_EXEC` file mappings |

The first Play attempt launched Box64 explicitly through the loader (`libldlinux.so … libbox64.so`). It failed exactly as predicted (`wine: could not exec the wine loader`), because Box64 re-execs `argv[0]` for every x86 child process. The fixed-path symlink removed the problem **without patching Box64**. Limitation: the `/data/data/<pkg>` path is the primary user's. Secondary users or work profiles would need `/data/user/<n>/…`, which is a later item.

## Issues found and fixed during Phase 0

1. Box64's `SD8EG5` option passes `-mtune=oryon-1`, which GCC 13 and 14.2 reject (Box64's comment says GCC 14; it's 15). The build drops that one flag and keeps the SM8850 `-march` set.
2. Extraction from `/sdcard/Android/data` took 107 s with 128 KiB reads (each call through FUSE is expensive). 4 MiB reads brought it to 1.9 s.
3. Android's `Process.waitFor(timeout)` polls every 100 ms, which skewed the first exit timings. The runner now uses a blocking wait in a helper thread.
4. An IDE silently upgraded the Gradle wrapper to 9.7.1, which breaks AGP 8.13. The wrapper is pinned back to 8.14.3 with a SHA-256.

## Harmless noise in the logs

`[BOX64] Unsupported libc Syscall 0x143` (userfaultfd), `nsi:poll_events bind failed, errno 13` (netlink is blocked for apps), `ZwLoadDriver … winebth` (no Bluetooth driver), `Didn't detect 48bits of address space, considering it's 39bits` (Android's 39-bit VA layout: fine for 64-bit Windows code, and worth remembering for very memory-hungry plugins). `PhantomProcessRecord … died` lines are normal process exits, not kills.

## Open for Phase 1

- PSS instead of RSS; trim session services; one long-lived session.
- Bridge host (winelib) loading real VST3s; offline MIDI → WAV render; compatibility table.
- Glibc is still Termux's prebuilt package (tracked deviation).
