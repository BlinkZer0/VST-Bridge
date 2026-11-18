# VST gHost: Phase 0 report

Generated 2026-11-18 09:40:19 on the device by the app (autorun). Every number below was measured on this device during this run.

## Device
- device: samsung SM-S948U1 (m3q)
- SoC: QTI SM8850
- Android 17 (API 37), patch 2026-10-05, build CP2A.260605.016.S948U1UEU5BZJA
- kernel: 6.12.69-android16-6-pa4ef354-abogkiS948USQU5BZJA-4k
- RAM: 11084.9 MB total, 3385.6 MB available at start
- app: com.vstghost 0.0.1-phase0, edition **play**, targetSdk 36
- running in emulator: no

## 1. Payload install
- source: `APK asset`
- archive size: 63.2 MB
- result: **OK**: Installed payload s0-20261118-box64-0.4.4-wine-11.0-glibc-glibc-2.44-0
- extractor stats: `{"files":2294,"dirs":146,"symlinks":19,"hardlinks":0,"bytes":427283087,"interpRewrites":1,"shebangRewrites":0,"symlinkRewrites":0,"ms":1851.5}`
- install wall time (extract + swap): **1954 ms**
- on-disk size of rootfs: 407.5 MB
- shim: linked 14 libraries

## 2. Box64 starts
**box64 --version**: exit=0, wall=3 ms, max guest processes=1, peak summed RSS=4.0 MB

```
$ /data/app/~~NA3MUhPLbejSV5E8HUvg4g==/com.vstghost-Qq6VHG5CCwBuTz6BMFotwA==/lib/arm64/libbox64.so --version
     3ms E Box64 arm64 v0.4.4 nogit with Dynarec built on Nov 18 2026 09:21:26
```

| pid | process | peak RSS (VmHWM) |
|---|---|---|
| 6122 | `/data/app/~~NA3MUhPLbejSV5E8HUvg4g==/com.vstghost-Qq6VHG5CCwBuTz6BMFotwA==/lib/arm64/libbox64.so --version` | 4.0 MB |

## 3. Wine loader
**wine --version**: exit=0, wall=19 ms, max guest processes=1, peak summed RSS=0.3 MB

```
$ /data/app/~~NA3MUhPLbejSV5E8HUvg4g==/com.vstghost-Qq6VHG5CCwBuTz6BMFotwA==/lib/arm64/libbox64.so /data/user/0/com.vstghost/files/rootfs/wine/bin/wine --version
     3ms E [BOX64] Dynarec for ARM64, with extension: ASIMD AES CRC32 PMULL ATOMICS SHA1 SHA2 USCAT FLAGM FLAGM2 FRINT AFP RNDR
     3ms E [BOX64] Running on Unknown CPU with 8 cores, pagesize: 4096
     3ms E [BOX64] Will use hardware counter measured at 19.2 MHz emulating 2.4 GHz
     3ms E [BOX64] Wine64 detected, WINEPRELOADRESERVE=""
     5ms E [BOX64] Didn't detect 48bits of address space, considering it's 39bits
     5ms E [BOX64] WINE prereserve of 0x10000:0x8000 done
     5ms E [BOX64] WINE prereserve of 0x110000:0x30000000 done
     5ms E [BOX64] WINE prereserve of 0x7f000000:0x3000000 done
     5ms E [BOX64] Counted 39 Env var
     5ms E [BOX64] Library search path: 
     5ms E [BOX64] Binary search path: /data/user/0/com.vstghost/files/rootfs/wine/bin/:/data/user/0/com.vstghost/files/rootfs/wine/bin/:/system/bin/
     5ms E [BOX64] Looking for /data/user/0/com.vstghost/files/rootfs/wine/bin/wine
     5ms E [BOX64] BOX64ENV: Variables overridden:
     5ms E 	BOX64_LD_LIBRARY_PATH=/data/user/0/com.vstghost/files/rootfs/box64/x64lib
     5ms E 	BOX64_LOG=1
     5ms E 	BOX64_PATH=/data/user/0/com.vstghost/files/rootfs/wine/bin
     5ms E 	BOX64_RCFILE=/data/user/0/com.vstghost/files/rootfs/etc/box64.box64rc
     5ms E 	BOX64_DYNACACHE=0
     5ms E [BOX64] argv[1]="--version"
     5ms E [BOX64] Rename process to "wine"
     5ms E [BOX64] Using native(wrapped) libdl.so.2
     6ms E [BOX64] Using native(wrapped) libc.so.6
     6ms E [BOX64] Using native(wrapped) ld-linux-x86-64.so.2
     6ms E [BOX64] Using native(wrapped) libpthread.so.0
     6ms E [BOX64] Using native(wrapped) libutil.so.1
     6ms E [BOX64] Using native(wrapped) libresolv.so.2
     6ms E [BOX64] Using native(wrapped) librt.so.1
     6ms E [BOX64] Using native(wrapped) libbsd.so.0
     9ms E [BOX64] Using emulated /data/data/com.vstghost/files/rootfs/wine/bin/../lib/wine/x86_64-unix/ntdll.so
     9ms E [BOX64] Using emulated /data/user/0/com.vstghost/files/rootfs/box64/x64lib/libgcc_s.so.1
    17ms   wine-11.0
```

| pid | process | peak RSS (VmHWM) |
|---|---|---|
| 6128 | `/data/app/~~NA3MUhPLbejSV5E8HUvg4g==/com.vstghost-Qq6VHG5CCwBuTz6BMFotwA==/lib/arm64/libbox64.so /data/user/0/com.vstghost/files/rootfs/wine/bin/wine --version` | 0.3 MB |

## 4. Cold start: `wine hello.exe` with no wineserver running
**cold**: exit=0, wall=1183 ms, max guest processes=8, peak summed RSS=439.5 MB

```
$ /data/app/~~NA3MUhPLbejSV5E8HUvg4g==/com.vstghost-Qq6VHG5CCwBuTz6BMFotwA==/lib/arm64/libbox64.so /data/user/0/com.vstghost/files/rootfs/wine/bin/wine /data/user/0/com.vstghost/files/rootfs/tests/hello.exe
     6ms E [BOX64] Dynarec for ARM64, with extension: ASIMD AES CRC32 PMULL ATOMICS SHA1 SHA2 USCAT FLAGM FLAGM2 FRINT AFP RNDR
     6ms E [BOX64] Running on Unknown CPU with 8 cores, pagesize: 4096
     6ms E [BOX64] Will use hardware counter measured at 19.2 MHz emulating 2.4 GHz
     6ms E [BOX64] Wine64 detected, WINEPRELOADRESERVE=""
     7ms E [BOX64] Didn't detect 48bits of address space, considering it's 39bits
     7ms E [BOX64] WINE prereserve of 0x10000:0x8000 done
     7ms E [BOX64] WINE prereserve of 0x110000:0x30000000 done
     8ms E [BOX64] WINE prereserve of 0x7f000000:0x3000000 done
     8ms E [BOX64] Detected running wine with "hello.exe"
     8ms E [BOX64] Counted 39 Env var
     8ms E [BOX64] Library search path: 
     8ms E [BOX64] Binary search path: /data/user/0/com.vstghost/files/rootfs/wine/bin/:/data/user/0/com.vstghost/files/rootfs/wine/bin/:/system/bin/
     8ms E [BOX64] Looking for /data/user/0/com.vstghost/files/rootfs/wine/bin/wine
     8ms E [BOX64] BOX64ENV: Variables overridden:
     8ms E 	BOX64_LD_LIBRARY_PATH=/data/user/0/com.vstghost/files/rootfs/box64/x64lib
     8ms E 	BOX64_LOG=1
     8ms E 	BOX64_PATH=/data/user/0/com.vstghost/files/rootfs/wine/bin
     8ms E 	BOX64_RCFILE=/data/user/0/com.vstghost/files/rootfs/etc/box64.box64rc
     8ms E 	BOX64_DYNACACHE=0
     8ms E [BOX64] argv[1]="/data/user/0/com.vstghost/files/rootfs/tests/hello.exe"
     8ms E [BOX64] Rename process to "wine"
     8ms E [BOX64] Using native(wrapped) libdl.so.2
     9ms E [BOX64] Using native(wrapped) libc.so.6
     9ms E [BOX64] Using native(wrapped) ld-linux-x86-64.so.2
     9ms E [BOX64] Using native(wrapped) libpthread.so.0
     9ms E [BOX64] Using native(wrapped) libutil.so.1
     9ms E [BOX64] Using native(wrapped) libresolv.so.2
     9ms E [BOX64] Using native(wrapped) librt.so.1
     9ms E [BOX64] Using native(wrapped) libbsd.so.0
    13ms E [BOX64] Using emulated /data/data/com.vstghost/files/rootfs/wine/bin/../lib/wine/x86_64-unix/ntdll.so
     0ms   … 298 lines omitted …
   751ms E 	BOX64_DYNACACHE=0
   751ms E [BOX64] argv[1]="C:\windows\system32\winedevice.exe"
   751ms E [BOX64] Rename process to "wine"
   751ms E [BOX64] Using native(wrapped) libdl.so.2
   751ms E [BOX64] Using native(wrapped) libc.so.6
   751ms E [BOX64] Using native(wrapped) ld-linux-x86-64.so.2
   752ms E [BOX64] Using native(wrapped) libpthread.so.0
   752ms E [BOX64] Using native(wrapped) libutil.so.1
   752ms E [BOX64] Using native(wrapped) libresolv.so.2
   752ms E [BOX64] Using native(wrapped) librt.so.1
   752ms E [BOX64] Using native(wrapped) libbsd.so.0
   755ms E [BOX64] Using emulated /data/data/com.vstghost/files/rootfs/wine/lib/wine/x86_64-unix/ntdll.so
   755ms E [BOX64] Using emulated /data/user/0/com.vstghost/files/rootfs/box64/x64lib/libgcc_s.so.1
   763ms E [BOX64] Warning: Unsupported libc Syscall 0x143 (323)
   852ms E [BOX64] Using emulated /data/data/com.vstghost/files/rootfs/wine/lib/wine/x86_64-unix/winebus.so
   886ms E [BOX64] Using emulated /data/data/com.vstghost/files/rootfs/wine/lib/wine/x86_64-unix/win32u.so
   887ms E [BOX64] Using native(wrapped) libm.so.6
   910ms E [BOX64] Using emulated /data/data/com.vstghost/files/rootfs/wine/lib/wine/x86_64-unix/winebth.so
   911ms E 00a4:err:ntoskrnl:ZwLoadDriver failed to create driver L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\winebth": c00000e5
   936ms E [BOX64] Using emulated /data/data/com.vstghost/files/rootfs/wine/lib/wine/x86_64-unix/win32u.so
   936ms E [BOX64] Using native(wrapped) libm.so.6
  1074ms   VSTGHOST-HELLO start
  1074ms   wine: 11.0
  1074ms   host: Linux 6.12.69-android16-6-pa4ef354-abogkiS948USQU5BZJA-4k
  1074ms   cpu:            Box64 v0.4.4 on Unknown CPU @4.74 GHz
  1074ms   avx2: yes
  1075ms   cpus: 8
  1075ms   memory: 11084 MB
  1177ms   BENCH ms=101.1 checksum=-407.86425463249907
  1177ms   VSTGHOST-HELLO done
```

| pid | process | peak RSS (VmHWM) |
|---|---|---|
| 6171 | `com.vstghost` | 195.5 MB |
| 6221 | `/data/data/com.vstghost/files/rootfs/wine/lib/wine/../../bin/wineserver` | 25.6 MB |
| 6262 | `wineboot.exe m.vstghost/files/rootfs/wine/lib/wine/x86_64-unix/wine C:\windows\system32\wineboot.exe --init` | 86.3 MB |
| 6279 | `services.exe m.vstghost/files/rootfs/wine/lib/wine/x86_64-unix/wine C:\windows\system32\services.exe` | 59.2 MB |
| 6282 | `winedevice.exe vstghost/files/rootfs/wine/lib/wine/x86_64-unix/wine C:\windows\system32\winedevice.exe` | 61.5 MB |
| 6301 | `plugplay.exe m.vstghost/files/rootfs/wine/lib/wine/x86_64-unix/wine C:\windows\system32\plugplay.exe` | 64.4 MB |
| 6311 | `svchost.exe om.vstghost/files/rootfs/wine/lib/wine/x86_64-unix/wine C:\windows\system32\svchost.exe -k LocalServiceNetworkRestricted` | 53.0 MB |
| 6320 | `winedevice.exe vstghost/files/rootfs/wine/lib/wine/x86_64-unix/wine C:\windows\system32\winedevice.exe` | 72.7 MB |

- exit=0; launch → first line of hello.exe output: **1074 ms**; → hello.exe done: 1177 ms; → wine exited: 1183 ms
- max guest processes: 8; peak summed RSS: **439.5 MB**
- benchmark: BENCH ms=101.1 checksum=-407.86425463249907
- guest processes all exited 1516 ms after hello.exe returned

## 5. Warm start: wineserver already running (`wineserver -p`)
- `wineserver -p` exit=0, server socket appeared after 0 ms
### Warm run 1
- exit=0; launch → first line of hello.exe output: **1329 ms**; → hello.exe done: 1437 ms; → wine exited: 1445 ms
- max guest processes: 8; peak summed RSS: **439.5 MB**
- benchmark: BENCH ms=104.1 checksum=-407.86425463249907
### Warm run 2
- exit=0; launch → first line of hello.exe output: **104 ms**; → hello.exe done: 205 ms; → wine exited: 212 ms
- max guest processes: 7; peak summed RSS: **380.5 MB**
- benchmark: BENCH ms=98.0 checksum=-407.86425463249907
### Warm run 3
- exit=0; launch → first line of hello.exe output: **93 ms**; → hello.exe done: 193 ms; → wine exited: 200 ms
- max guest processes: 7; peak summed RSS: **379.8 MB**
- benchmark: BENCH ms=97.1 checksum=-407.86425463249907
**warm run 1 (full output)**: exit=0, wall=1445 ms, max guest processes=8, peak summed RSS=439.5 MB

```
$ /data/app/~~NA3MUhPLbejSV5E8HUvg4g==/com.vstghost-Qq6VHG5CCwBuTz6BMFotwA==/lib/arm64/libbox64.so /data/user/0/com.vstghost/files/rootfs/wine/bin/wine /data/user/0/com.vstghost/files/rootfs/tests/hello.exe
    10ms E [BOX64] Dynarec for ARM64, with extension: ASIMD AES CRC32 PMULL ATOMICS SHA1 SHA2 USCAT FLAGM FLAGM2 FRINT AFP RNDR
    10ms E [BOX64] Running on Unknown CPU with 8 cores, pagesize: 4096
    10ms E [BOX64] Will use hardware counter measured at 19.2 MHz emulating 2.4 GHz
    10ms E [BOX64] Wine64 detected, WINEPRELOADRESERVE=""
    16ms E [BOX64] Didn't detect 48bits of address space, considering it's 39bits
    16ms E [BOX64] WINE prereserve of 0x10000:0x8000 done
    16ms E [BOX64] WINE prereserve of 0x110000:0x30000000 done
    16ms E [BOX64] WINE prereserve of 0x7f000000:0x3000000 done
    16ms E [BOX64] Detected running wine with "hello.exe"
    16ms E [BOX64] Counted 39 Env var
    16ms E [BOX64] Library search path: 
    16ms E [BOX64] Binary search path: /data/user/0/com.vstghost/files/rootfs/wine/bin/:/data/user/0/com.vstghost/files/rootfs/wine/bin/:/system/bin/
    16ms E [BOX64] Looking for /data/user/0/com.vstghost/files/rootfs/wine/bin/wine
    16ms E [BOX64] BOX64ENV: Variables overridden:
    16ms E 	BOX64_LD_LIBRARY_PATH=/data/user/0/com.vstghost/files/rootfs/box64/x64lib
    16ms E 	BOX64_LOG=1
    16ms E 	BOX64_PATH=/data/user/0/com.vstghost/files/rootfs/wine/bin
    16ms E 	BOX64_RCFILE=/data/user/0/com.vstghost/files/rootfs/etc/box64.box64rc
    16ms E 	BOX64_DYNACACHE=0
    16ms E [BOX64] argv[1]="/data/user/0/com.vstghost/files/rootfs/tests/hello.exe"
    16ms E [BOX64] Rename process to "wine"
    16ms E [BOX64] Using native(wrapped) libdl.so.2
    17ms E [BOX64] Using native(wrapped) libc.so.6
    17ms E [BOX64] Using native(wrapped) ld-linux-x86-64.so.2
    17ms E [BOX64] Using native(wrapped) libpthread.so.0
    17ms E [BOX64] Using native(wrapped) libutil.so.1
    17ms E [BOX64] Using native(wrapped) libresolv.so.2
    17ms E [BOX64] Using native(wrapped) librt.so.1
    17ms E [BOX64] Using native(wrapped) libbsd.so.0
    21ms E [BOX64] Using emulated /data/data/com.vstghost/files/rootfs/wine/bin/../lib/wine/x86_64-unix/ntdll.so
     0ms   … 273 lines omitted …
   937ms E 	BOX64_DYNACACHE=0
   937ms E [BOX64] argv[1]="C:\windows\system32\winedevice.exe"
   937ms E [BOX64] Rename process to "wine"
   937ms E [BOX64] Using native(wrapped) libdl.so.2
   938ms E [BOX64] Using native(wrapped) libc.so.6
   938ms E [BOX64] Using native(wrapped) ld-linux-x86-64.so.2
   938ms E [BOX64] Using native(wrapped) libpthread.so.0
   938ms E [BOX64] Using native(wrapped) libutil.so.1
   938ms E [BOX64] Using native(wrapped) libresolv.so.2
   938ms E [BOX64] Using native(wrapped) librt.so.1
   938ms E [BOX64] Using native(wrapped) libbsd.so.0
   940ms E [BOX64] Using emulated /data/data/com.vstghost/files/rootfs/wine/lib/wine/x86_64-unix/ntdll.so
   941ms E [BOX64] Using emulated /data/user/0/com.vstghost/files/rootfs/box64/x64lib/libgcc_s.so.1
   948ms E [BOX64] Warning: Unsupported libc Syscall 0x143 (323)
  1091ms E [BOX64] Using emulated /data/data/com.vstghost/files/rootfs/wine/lib/wine/x86_64-unix/winebus.so
  1128ms E [BOX64] Using emulated /data/data/com.vstghost/files/rootfs/wine/lib/wine/x86_64-unix/win32u.so
  1128ms E [BOX64] Using native(wrapped) libm.so.6
  1143ms E [BOX64] Using emulated /data/data/com.vstghost/files/rootfs/wine/lib/wine/x86_64-unix/winebth.so
  1144ms E 00a4:err:ntoskrnl:ZwLoadDriver failed to create driver L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\winebth": c00000e5
  1168ms E [BOX64] Using emulated /data/data/com.vstghost/files/rootfs/wine/lib/wine/x86_64-unix/win32u.so
  1168ms E [BOX64] Using native(wrapped) libm.so.6
  1329ms   VSTGHOST-HELLO start
  1330ms   wine: 11.0
  1330ms   host: Linux 6.12.69-android16-6-pa4ef354-abogkiS948USQU5BZJA-4k
  1330ms   cpu:            Box64 v0.4.4 on Unknown CPU @4.74 GHz
  1330ms   avx2: yes
  1331ms   cpus: 8
  1332ms   memory: 11084 MB
  1437ms   BENCH ms=104.1 checksum=-407.86425463249907
  1437ms   VSTGHOST-HELLO done
```

| pid | process | peak RSS (VmHWM) |
|---|---|---|
| 6520 | `/data/user/0/com.vstghost/files/rootfs/wine/bin/wineserver -p` | 25.6 MB |
| 6536 | `hello.exe /com.vstghost/files/rootfs/wine/lib/wine/x86_64-unix/wine /data/user/0/com.vstghost/files/rootfs/tests/hello.exe` | 45.1 MB |
| 6548 | `wineboot.exe m.vstghost/files/rootfs/wine/lib/wine/x86_64-unix/wine C:\windows\system32\wineboot.exe --init` | 86.5 MB |
| 6567 | `services.exe m.vstghost/files/rootfs/wine/lib/wine/x86_64-unix/wine C:\windows\system32\services.exe` | 58.5 MB |
| 6573 | `winedevice.exe vstghost/files/rootfs/wine/lib/wine/x86_64-unix/wine C:\windows\system32\winedevice.exe` | 62.2 MB |
| 6591 | `plugplay.exe m.vstghost/files/rootfs/wine/lib/wine/x86_64-unix/wine C:\windows\system32\plugplay.exe` | 64.4 MB |
| 6609 | `svchost.exe om.vstghost/files/rootfs/wine/lib/wine/x86_64-unix/wine C:\windows\system32\svchost.exe -k LocalServiceNetworkRestricted` | 52.9 MB |
| 6618 | `winedevice.exe vstghost/files/rootfs/wine/lib/wine/x86_64-unix/wine C:\windows\system32\winedevice.exe` | 71.1 MB |

## 6. Box64 overhead on DSP code
- native arm64 (best of 3: 104.4, 82.0, 82.2 ms): **82.0 ms**, checksum `-407.86425463249907`
- note: neither run is pinned to a core; differences of ±20% can be big-vs-mid core scheduling, not Box64
- x86_64 under Box64 + Wine (best of 4): **97.1 ms**, checksum `-407.86425463249907`
- **overhead factor: 1.18×**
- checksums **match** (same floating-point results)
- the kernel renders 60 s of 32 voices at 48 kHz; real-time factor under Box64 ≈ 618× (single core)

---
Logcat tag: `VSTGHOST`. Look for SELinux denials with `adb logcat -d | grep avc`.
