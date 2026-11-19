# Phase 5: test steps (native plugins, imported Windows plugins, editors)

Prerequisites: Phase 2 setup; payload series 2 built (`payload/build.sh`, needs the packages from
`payload/setup-ubuntu.sh` including `llvm-18-dev libpolly-18-dev`).

## Build and install

```powershell
$env:JAVA_HOME = "C:\Program Files\Android\openjdk\jdk-21.0.8"
# Odin2 does not build natively yet (modal loops): build the other three.
.\gradlew.bat assemblePlayDebug "-Pvg.nativePlugins=epiano;brass;pedalfaster;nostrings;theham" "-Pvg.pluginSrcRoot=C:/Users/Randy/OneDrive/Documents/GitHub"
$adb = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
& $adb install -r app\build\outputs\apk\play\debug\app-play-debug.apk
& $adb shell am start -n com.vstghost/com.vstghost.PlayerActivity
# First start extracts the payload (~2 min). Wait for it; don't force-stop meanwhile.
& $adb shell run-as com.vstghost cat files/rootfs/.installed   # prints the payload version
```

## Import Windows plugins

By hand: **Add** in the player, pick a `.vst3` file, a zip containing one, or a `.vst3` folder.
Over adb (zip already on the phone):

```powershell
& $adb shell am start -n com.vstghost/com.vstghost.PlayerActivity --es importZip /sdcard/Download/Dexed.zip
```

## Native plugins

1. Pick **E-Piano (native)**, Load, play the on-screen keyboard: sound, `stats:` lines in logcat show
   `hostAvg` of tens of µs.
2. **Editor**: the native editor opens full screen; pinch to zoom, drag to pan; the instrument
   selector (a web UI) works. Close and reopen: the editor opens again.
3. `--es selftest 1` on the player runs the virtual-MIDI self test with native slots (expect PASS).

adb extras run once: the player is `singleTask`, and Android replays a dead task's launch intent on
the next start, so an identical command is ignored — add a changing extra (`--el t <seconds>`) to
repeat one. While the app runs, `--ei slot N`, `--es load <name>` and `--es req "cmd\narg"` (a
request to the selected slot, answer in logcat) are delivered to the running player. The player's
slots are Inst 1–4 (engine slots 1–4) and FX 1–4 (5–8); `--es load` goes to the selected slot
(Inst 1 at start), so editor dumps use `--ei slot 1`.

## Bridged editors (automated, works with the screen locked)

```powershell
& $adb shell am start -n com.vstghost/com.vstghost.PlayerActivity --es load Dexed
Start-Sleep 35
& $adb shell "am start -n com.vstghost/com.vstghost.EditorActivity --ei slot 1 --ei dump 5 --es taps '420,560/150,140'"
Start-Sleep 60
& $adb logcat -d -s VSTGHOST:* | Select-String "editor_open|editor dump|tap|underruns"
cmd /c "`"$adb`" exec-out run-as com.vstghost cat files/editor-dump-4.bmp > dexed.bmp"
python tools/phone/bmp2png.py dexed.bmp dexed.png
```

Expected for **Dexed**: `editor_open … "ok":true` in 12–14 s; dexed.png shows the whole editor
(six operator panels with knobs, the LCD, the keyboard); after the first tap the LCD shows
"OP1 L Scale Depth: 0"; `underruns 0` or 1.

Same for **Pedal Faster** (`--es load "'Pedal Faster'"`, 1400×860, opens in ~2 s) and
**Surge XT** (`--es load "'Surge XT'"`, 1141×711 OpenGL editor, opens in ~2 s; `info` reports
2081 parameters).

## Debugging aids

- `files/env.debug` (debug builds): `KEY=VALUE` lines added to every Wine process's environment,
  e.g. `WINEDEBUG=-all,+dxgi` or `VSTGHOST_VBLANK_MS=100`. Push it with
  `adb push env.debug /data/local/tmp/ && adb shell run-as com.vstghost cp /data/local/tmp/env.debug files/`.
- `VSTGHOST_DXGI_DUMP=Z:/data/user/0/com.vstghost/files/frame` writes the first presented Direct2D
  frames as `frame-<n>.bmp` (what dxgi hands to the window) and `frame-rb-<n>.bmp` (what GDI holds).
- `tools/phone/phone-run.sh`: run any Wine command on the phone with the app's environment
  (`adb shell run-as com.vstghost env LIBDIR=<nativeLibraryDir> sh files/phone-run.sh <WINEDEBUG> <exe> …`);
  `tools/phone/phone-editor-shot.sh <tag>` opens a plugin's editor with the host alone and saves a
  GDI screenshot; `tools/phone/dlltest.c` loads DLLs one by one (the patch 0004 repro).
- Reinstalling the APK re-extracts the payload and overwrites hand-pushed files.

## PC (no phone)

`payload/build.sh` + the WSL scripts from Phase 4 still work; the Direct2D investigation used the
compositor test driver with timed clicks (`tools/wl/vgwl_test … "click:14:150:140" "shot:36:tag"`).
