# VST gHost: Phase 0 report

Generated 2026-11-18 09:40:36 on the device by the app (autorun). Every number below was measured on this device during this run.

## Device
- device: samsung SM-S948U1 (m3q)
- SoC: QTI SM8850
- Android 17 (API 37), patch 2026-10-05, build CP2A.260605.016.S948U1UEU5BZJA
- kernel: 6.12.69-android16-6-pa4ef354-abogkiS948USQU5BZJA-4k
- RAM: 11084.9 MB total, 3409.9 MB available at start
- app: com.vstghost.sideload 0.0.1-phase0, edition **sideload**, targetSdk 28
- running in emulator: no

## 1. Payload install
- source: `/storage/emulated/0/Android/data/com.vstghost.sideload/files/vstghost-payload.tar.zst`
- archive size: 63.2 MB
- result: **OK**: Installed payload s0-20261118-box64-0.4.4-wine-11.0-glibc-glibc-2.44-0
- extractor stats: `{"files":2294,"dirs":146,"symlinks":19,"hardlinks":0,"bytes":427283087,"interpRewrites":1,"shebangRewrites":0,"symlinkRewrites":0,"ms":1857.2}`
- install wall time (extract + swap): **1938 ms**
- on-disk size of rootfs: 407.5 MB

## 2. Box64 starts
**box64 --version**: exit=0, wall=3 ms, max guest processes=1, peak summed RSS=4.0 MB

```
$ /data/user/0/com.vstghost.sideload/files/rootfs/box64/bin/box64 --version
     3ms E Box64 arm64 v0.4.4 nogit with Dynarec built on Nov 18 2026 09:21:26
```

| pid | process | peak RSS (VmHWM) |
|---|---|---|
| 6918 | `/data/user/0/com.vstghost.sideload/files/rootfs/box64/bin/box64 --version` | 4.0 MB |

## 3. Wine loader
**wine --version**: exit=0, wall=16 ms, max guest processes=1, peak summed RSS=5.1 MB

```
$ /data/user/0/com.vstghost.sideload/files/rootfs/box64/bin/box64 /data/user/0/com.vstghost.sideload/files/rootfs/wine/bin/wine --version
     2ms E [BOX64] Dynarec for ARM64, with extension: ASIMD AES CRC32 PMULL ATOMICS SHA1 SHA2 USCAT FLAGM FLAGM2 FRINT AFP RNDR
     2ms E [BOX64] Running on Unknown CPU with 8 cores, pagesize: 4096
     2ms E [BOX64] Will use hardware counter measured at 19.2 MHz emulating 2.4 GHz
     2ms E [BOX64] Wine64 detected, WINEPRELOADRESERVE=""
     4ms E [BOX64] Didn't detect 48bits of address space, considering it's 39bits
     4ms E [BOX64] WINE prereserve of 0x10000:0x8000 done
     4ms E [BOX64] WINE prereserve of 0x110000:0x30000000 done
     4ms E [BOX64] WINE prereserve of 0x7f000000:0x3000000 done
     4ms E [BOX64] Counted 39 Env var
     4ms E [BOX64] Library search path: 
     4ms E [BOX64] Binary search path: /data/user/0/com.vstghost.sideload/files/rootfs/wine/bin/:/data/user/0/com.vstghost.sideload/files/rootfs/wine/bin/:/system/bin/
     4ms E [BOX64] Looking for /data/user/0/com.vstghost.sideload/files/rootfs/wine/bin/wine
     4ms E [BOX64] BOX64ENV: Variables overridden:
     4ms E 	BOX64_LD_LIBRARY_PATH=/data/user/0/com.vstghost.sideload/files/rootfs/box64/x64lib
     4ms E 	BOX64_LOG=1
     4ms E 	BOX64_PATH=/data/user/0/com.vstghost.sideload/files/rootfs/wine/bin
     4ms E 	BOX64_RCFILE=/data/user/0/com.vstghost.sideload/files/rootfs/etc/box64.box64rc
     4ms E 	BOX64_DYNACACHE=0
     4ms E [BOX64] argv[1]="--version"
     4ms E [BOX64] Rename process to "wine"
     4ms E [BOX64] Using native(wrapped) libdl.so.2
     5ms E [BOX64] Using native(wrapped) libc.so.6
     5ms E [BOX64] Using native(wrapped) ld-linux-x86-64.so.2
     5ms E [BOX64] Using native(wrapped) libpthread.so.0
     5ms E [BOX64] Using native(wrapped) libutil.so.1
     5ms E [BOX64] Using native(wrapped) libresolv.so.2
     5ms E [BOX64] Using native(wrapped) librt.so.1
     5ms E [BOX64] Using native(wrapped) libbsd.so.0
     8ms E [BOX64] Using emulated /data/user/0/com.vstghost.sideload/files/rootfs/wine/bin/../lib/wine/x86_64-unix/ntdll.so
     8ms E [BOX64] Using emulated /data/user/0/com.vstghost.sideload/files/rootfs/box64/x64lib/libgcc_s.so.1
    15ms   wine-11.0
```

| pid | process | peak RSS (VmHWM) |
|---|---|---|
| 6934 | `/data/user/0/com.vstghost.sideload/files/rootfs/box64/bin/box64 /data/user/0/com.vstghost.sideload/files/rootfs/wine/bin/wine --version` | 5.1 MB |

## 4. Cold start: `wine hello.exe` with no wineserver running
**cold**: exit=0, wall=1192 ms, max guest processes=8, peak summed RSS=445.4 MB

```
$ /data/user/0/com.vstghost.sideload/files/rootfs/box64/bin/box64 /data/user/0/com.vstghost.sideload/files/rootfs/wine/bin/wine /data/user/0/com.vstghost.sideload/files/rootfs/tests/hello.exe
     4ms E [BOX64] Dynarec for ARM64, with extension: ASIMD AES CRC32 PMULL ATOMICS SHA1 SHA2 USCAT FLAGM FLAGM2 FRINT AFP RNDR
     4ms E [BOX64] Running on Unknown CPU with 8 cores, pagesize: 4096
     4ms E [BOX64] Will use hardware counter measured at 19.2 MHz emulating 2.4 GHz
     4ms E [BOX64] Wine64 detected, WINEPRELOADRESERVE=""
     5ms E [BOX64] Didn't detect 48bits of address space, considering it's 39bits
     5ms E [BOX64] WINE prereserve of 0x10000:0x8000 done
     6ms E [BOX64] WINE prereserve of 0x110000:0x30000000 done
     6ms E [BOX64] WINE prereserve of 0x7f000000:0x3000000 done
     6ms E [BOX64] Detected running wine with "hello.exe"
     6ms E [BOX64] Counted 39 Env var
     6ms E [BOX64] Library search path: 
     6ms E [BOX64] Binary search path: /data/user/0/com.vstghost.sideload/files/rootfs/wine/bin/:/data/user/0/com.vstghost.sideload/files/rootfs/wine/bin/:/system/bin/
     6ms E [BOX64] Looking for /data/user/0/com.vstghost.sideload/files/rootfs/wine/bin/wine
     6ms E [BOX64] BOX64ENV: Variables overridden:
     6ms E 	BOX64_LD_LIBRARY_PATH=/data/user/0/com.vstghost.sideload/files/rootfs/box64/x64lib
     6ms E 	BOX64_LOG=1
     6ms E 	BOX64_PATH=/data/user/0/com.vstghost.sideload/files/rootfs/wine/bin
     6ms E 	BOX64_RCFILE=/data/user/0/com.vstghost.sideload/files/rootfs/etc/box64.box64rc
     6ms E 	BOX64_DYNACACHE=0
     6ms E [BOX64] argv[1]="/data/user/0/com.vstghost.sideload/files/rootfs/tests/hello.exe"
     6ms E [BOX64] Rename process to "wine"
     6ms E [BOX64] Using native(wrapped) libdl.so.2
     6ms E [BOX64] Using native(wrapped) libc.so.6
     6ms E [BOX64] Using native(wrapped) ld-linux-x86-64.so.2
     6ms E [BOX64] Using native(wrapped) libpthread.so.0
     6ms E [BOX64] Using native(wrapped) libutil.so.1
     6ms E [BOX64] Using native(wrapped) libresolv.so.2
     6ms E [BOX64] Using native(wrapped) librt.so.1
     6ms E [BOX64] Using native(wrapped) libbsd.so.0
    10ms E [BOX64] Using emulated /data/user/0/com.vstghost.sideload/files/rootfs/wine/bin/../lib/wine/x86_64-unix/ntdll.so
     0ms   … 298 lines omitted …
   765ms E 	BOX64_DYNACACHE=0
   765ms E [BOX64] argv[1]="C:\windows\system32\winedevice.exe"
   766ms E [BOX64] Rename process to "wine"
   766ms E [BOX64] Using native(wrapped) libdl.so.2
   766ms E [BOX64] Using native(wrapped) libc.so.6
   766ms E [BOX64] Using native(wrapped) ld-linux-x86-64.so.2
   766ms E [BOX64] Using native(wrapped) libpthread.so.0
   766ms E [BOX64] Using native(wrapped) libutil.so.1
   766ms E [BOX64] Using native(wrapped) libresolv.so.2
   766ms E [BOX64] Using native(wrapped) librt.so.1
   766ms E [BOX64] Using native(wrapped) libbsd.so.0
   770ms E [BOX64] Using emulated /data/user/0/com.vstghost.sideload/files/rootfs/wine/lib/wine/x86_64-unix/ntdll.so
   770ms E [BOX64] Using emulated /data/user/0/com.vstghost.sideload/files/rootfs/box64/x64lib/libgcc_s.so.1
   776ms E [BOX64] Warning: Unsupported libc Syscall 0x143 (323)
   875ms E [BOX64] Using emulated /data/user/0/com.vstghost.sideload/files/rootfs/wine/lib/wine/x86_64-unix/winebus.so
   914ms E [BOX64] Using emulated /data/user/0/com.vstghost.sideload/files/rootfs/wine/lib/wine/x86_64-unix/win32u.so
   914ms E [BOX64] Using native(wrapped) libm.so.6
   927ms E [BOX64] Using emulated /data/user/0/com.vstghost.sideload/files/rootfs/wine/lib/wine/x86_64-unix/winebth.so
   928ms E 00a4:err:ntoskrnl:ZwLoadDriver failed to create driver L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\winebth": c00000e5
   952ms E [BOX64] Using emulated /data/user/0/com.vstghost.sideload/files/rootfs/wine/lib/wine/x86_64-unix/win32u.so
   952ms E [BOX64] Using native(wrapped) libm.so.6
  1087ms   VSTGHOST-HELLO start
  1087ms   wine: 11.0
  1087ms   host: Linux 6.12.69-android16-6-pa4ef354-abogkiS948USQU5BZJA-4k
  1088ms   cpu:            Box64 v0.4.4 on Unknown CPU @4.74 GHz
  1088ms   avx2: yes
  1088ms   cpus: 8
  1088ms   memory: 11084 MB
  1187ms   BENCH ms=97.7 checksum=-407.86425463249907
  1187ms   VSTGHOST-HELLO done
```

| pid | process | peak RSS (VmHWM) |
|---|---|---|
| 6981 | `hello.exe /0/com.vstghost.sideload/files/rootfs/wine/lib/wine/x86_64-unix/wine /data/user/0/com.vstghost.sideload/files/rootfs/tests/hello.exe` | 45.6 MB |
| 6989 | `/data/user/0/com.vstghost.sideload/files/rootfs/wine/lib/wine/../../bin/wineserver` | 25.8 MB |
| 6992 | `wineboot.exe com.vstghost.sideload/files/rootfs/wine/lib/wine/x86_64-unix/wine C:\windows\system32\wineboot.exe --init` | 82.2 MB |
| 6996 | `/data/user/0/com.vstghost.sideload/files/rootfs/box64/bin/box64 /data/user/0/com.vstghost.sideload/files/rootfs/wine/lib/wine/x86_64-unix/wine-preloader /data/u` | 7.0 MB |
| 6998 | `services.exe com.vstghost.sideload/files/rootfs/wine/lib/wine/x86_64-unix/wine C:\windows\system32\services.exe` | 59.5 MB |
| 7001 | `winedevice.exe m.vstghost.sideload/files/rootfs/wine/lib/wine/x86_64-unix/wine C:\windows\system32\winedevice.exe` | 62.8 MB |
| 7023 | `plugplay.exe com.vstghost.sideload/files/rootfs/wine/lib/wine/x86_64-unix/wine C:\windows\system32\plugplay.exe` | 64.2 MB |
| 7033 | `svchost.exe /com.vstghost.sideload/files/rootfs/wine/lib/wine/x86_64-unix/wine C:\windows\system32\svchost.exe -k LocalServiceNetworkRestricted` | 54.1 MB |
| 7049 | `winedevice.exe m.vstghost.sideload/files/rootfs/wine/lib/wine/x86_64-unix/wine C:\windows\system32\winedevice.exe` | 71.0 MB |

- exit=0; launch → first line of hello.exe output: **1087 ms**; → hello.exe done: 1187 ms; → wine exited: 1192 ms
- max guest processes: 8; peak summed RSS: **445.4 MB**
- benchmark: BENCH ms=97.7 checksum=-407.86425463249907
- guest processes all exited 1505 ms after hello.exe returned

## 5. Warm start: wineserver already running (`wineserver -p`)
- `wineserver -p` exit=0, server socket appeared after 0 ms
### Warm run 1
- exit=0; launch → first line of hello.exe output: **1311 ms**; → hello.exe done: 1411 ms; → wine exited: 1419 ms
- max guest processes: 8; peak summed RSS: **449.4 MB**
- benchmark: BENCH ms=97.3 checksum=-407.86425463249907
### Warm run 2
- exit=0; launch → first line of hello.exe output: **104 ms**; → hello.exe done: 203 ms; → wine exited: 211 ms
- max guest processes: 7; peak summed RSS: **382.2 MB**
- benchmark: BENCH ms=96.7 checksum=-407.86425463249907
### Warm run 3
- exit=0; launch → first line of hello.exe output: **139 ms**; → hello.exe done: 241 ms; → wine exited: 248 ms
- max guest processes: 7; peak summed RSS: **382.2 MB**
- benchmark: BENCH ms=99.6 checksum=-407.86425463249907
**warm run 1 (full output)**: exit=0, wall=1419 ms, max guest processes=8, peak summed RSS=449.4 MB

```
$ /data/user/0/com.vstghost.sideload/files/rootfs/box64/bin/box64 /data/user/0/com.vstghost.sideload/files/rootfs/wine/bin/wine /data/user/0/com.vstghost.sideload/files/rootfs/tests/hello.exe
     8ms E [BOX64] Dynarec for ARM64, with extension: ASIMD AES CRC32 PMULL ATOMICS SHA1 SHA2 USCAT FLAGM FLAGM2 FRINT AFP RNDR
     8ms E [BOX64] Running on Unknown CPU with 8 cores, pagesize: 4096
     8ms E [BOX64] Will use hardware counter measured at 19.2 MHz emulating 2.4 GHz
     8ms E [BOX64] Wine64 detected, WINEPRELOADRESERVE=""
    11ms E [BOX64] Didn't detect 48bits of address space, considering it's 39bits
    11ms E [BOX64] WINE prereserve of 0x10000:0x8000 done
    11ms E [BOX64] WINE prereserve of 0x110000:0x30000000 done
    11ms E [BOX64] WINE prereserve of 0x7f000000:0x3000000 done
    11ms E [BOX64] Detected running wine with "hello.exe"
    11ms E [BOX64] Counted 39 Env var
    11ms E [BOX64] Library search path: 
    11ms E [BOX64] Binary search path: /data/user/0/com.vstghost.sideload/files/rootfs/wine/bin/:/data/user/0/com.vstghost.sideload/files/rootfs/wine/bin/:/system/bin/
    11ms E [BOX64] Looking for /data/user/0/com.vstghost.sideload/files/rootfs/wine/bin/wine
    12ms E [BOX64] BOX64ENV: Variables overridden:
    12ms E 	BOX64_LD_LIBRARY_PATH=/data/user/0/com.vstghost.sideload/files/rootfs/box64/x64lib
    12ms E 	BOX64_LOG=1
    12ms E 	BOX64_PATH=/data/user/0/com.vstghost.sideload/files/rootfs/wine/bin
    12ms E 	BOX64_RCFILE=/data/user/0/com.vstghost.sideload/files/rootfs/etc/box64.box64rc
    12ms E 	BOX64_DYNACACHE=0
    12ms E [BOX64] argv[1]="/data/user/0/com.vstghost.sideload/files/rootfs/tests/hello.exe"
    12ms E [BOX64] Rename process to "wine"
    12ms E [BOX64] Using native(wrapped) libdl.so.2
    12ms E [BOX64] Using native(wrapped) libc.so.6
    12ms E [BOX64] Using native(wrapped) ld-linux-x86-64.so.2
    12ms E [BOX64] Using native(wrapped) libpthread.so.0
    12ms E [BOX64] Using native(wrapped) libutil.so.1
    12ms E [BOX64] Using native(wrapped) libresolv.so.2
    12ms E [BOX64] Using native(wrapped) librt.so.1
    12ms E [BOX64] Using native(wrapped) libbsd.so.0
    16ms E [BOX64] Using emulated /data/user/0/com.vstghost.sideload/files/rootfs/wine/bin/../lib/wine/x86_64-unix/ntdll.so
     0ms   … 273 lines omitted …
   976ms E 	BOX64_DYNACACHE=0
   976ms E [BOX64] argv[1]="C:\windows\system32\winedevice.exe"
   977ms E [BOX64] Rename process to "wine"
   977ms E [BOX64] Using native(wrapped) libdl.so.2
   977ms E [BOX64] Using native(wrapped) libc.so.6
   977ms E [BOX64] Using native(wrapped) ld-linux-x86-64.so.2
   977ms E [BOX64] Using native(wrapped) libpthread.so.0
   977ms E [BOX64] Using native(wrapped) libutil.so.1
   977ms E [BOX64] Using native(wrapped) libresolv.so.2
   977ms E [BOX64] Using native(wrapped) librt.so.1
   977ms E [BOX64] Using native(wrapped) libbsd.so.0
   981ms E [BOX64] Using emulated /data/user/0/com.vstghost.sideload/files/rootfs/wine/lib/wine/x86_64-unix/ntdll.so
   981ms E [BOX64] Using emulated /data/user/0/com.vstghost.sideload/files/rootfs/box64/x64lib/libgcc_s.so.1
   990ms E [BOX64] Warning: Unsupported libc Syscall 0x143 (323)
  1116ms E [BOX64] Using emulated /data/user/0/com.vstghost.sideload/files/rootfs/wine/lib/wine/x86_64-unix/winebus.so
  1148ms E [BOX64] Using emulated /data/user/0/com.vstghost.sideload/files/rootfs/wine/lib/wine/x86_64-unix/win32u.so
  1148ms E [BOX64] Using native(wrapped) libm.so.6
  1161ms E [BOX64] Using emulated /data/user/0/com.vstghost.sideload/files/rootfs/wine/lib/wine/x86_64-unix/winebth.so
  1162ms E 00a4:err:ntoskrnl:ZwLoadDriver failed to create driver L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\winebth": c00000e5
  1185ms E [BOX64] Using emulated /data/user/0/com.vstghost.sideload/files/rootfs/wine/lib/wine/x86_64-unix/win32u.so
  1185ms E [BOX64] Using native(wrapped) libm.so.6
  1311ms   VSTGHOST-HELLO start
  1311ms   wine: 11.0
  1311ms   host: Linux 6.12.69-android16-6-pa4ef354-abogkiS948USQU5BZJA-4k
  1311ms   cpu:            Box64 v0.4.4 on Unknown CPU @4.74 GHz
  1311ms   avx2: yes
  1312ms   cpus: 8
  1312ms   memory: 11084 MB
  1410ms   BENCH ms=97.3 checksum=-407.86425463249907
  1411ms   VSTGHOST-HELLO done
```

| pid | process | peak RSS (VmHWM) |
|---|---|---|
| 7116 | `/data/user/0/com.vstghost.sideload/files/rootfs/wine/bin/wineserver -p` | 25.8 MB |
| 7125 | `hello.exe /0/com.vstghost.sideload/files/rootfs/wine/lib/wine/x86_64-unix/wine /data/user/0/com.vstghost.sideload/files/rootfs/tests/hello.exe` | 46.3 MB |
| 7136 | `wineboot.exe com.vstghost.sideload/files/rootfs/wine/lib/wine/x86_64-unix/wine C:\windows\system32\wineboot.exe --init` | 86.6 MB |
| 7143 | `services.exe com.vstghost.sideload/files/rootfs/wine/lib/wine/x86_64-unix/wine C:\windows\system32\services.exe` | 59.0 MB |
| 7148 | `winedevice.exe m.vstghost.sideload/files/rootfs/wine/lib/wine/x86_64-unix/wine C:\windows\system32\winedevice.exe` | 62.0 MB |
| 7162 | `plugplay.exe com.vstghost.sideload/files/rootfs/wine/lib/wine/x86_64-unix/wine C:\windows\system32\plugplay.exe` | 64.2 MB |
| 7172 | `svchost.exe /com.vstghost.sideload/files/rootfs/wine/lib/wine/x86_64-unix/wine C:\windows\system32\svchost.exe -k LocalServiceNetworkRestricted` | 54.1 MB |
| 7181 | `winedevice.exe m.vstghost.sideload/files/rootfs/wine/lib/wine/x86_64-unix/wine C:\windows\system32\winedevice.exe` | 71.0 MB |

## 6. Box64 overhead on DSP code
- native arm64 (best of 3: 102.2, 82.4, 82.6 ms): **82.4 ms**, checksum `-407.86425463249907`
- note: neither run is pinned to a core; differences of ±20% can be big-vs-mid core scheduling, not Box64
- x86_64 under Box64 + Wine (best of 4): **96.7 ms**, checksum `-407.86425463249907`
- **overhead factor: 1.17×**
- checksums **match** (same floating-point results)
- the kernel renders 60 s of 32 voices at 48 kHz; real-time factor under Box64 ≈ 620× (single core)

---
Logcat tag: `VSTGHOST`. Look for SELinux denials with `adb logcat -d | grep avc`.
