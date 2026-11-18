# Phase 4: test steps (plugin editors)

Prerequisites: Phase 2 setup; the payload rebuilt (`payload/build.sh`), plugins pushed as before.

## Build and install

```powershell
$env:JAVA_HOME = "C:\Program Files\Android\openjdk\jdk-21.0.8"
.\gradlew.bat assemblePlayDebug assembleSideloadDebug
$adb = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
& $adb install -r app\build\outputs\apk\play\debug\app-play-debug.apk
# Sideload edition only: push the new payload; the app re-extracts it because it changed.
& $adb push payload\out\vstghost-payload.tar.zst /sdcard/Android/data/com.vstghost.sideload/files/
```

## By hand

1. Open VST gHost, pick **Odin2**, press **Load**.
2. Press **Editor**. After ~7 s the Odin2 editor appears, scaled to fit.
3. Turn a knob (drag up/down), open a menu (small ▼ arrows), play the on-screen keyboard in the
   player afterwards: the sound follows the editor's settings.
4. **Close** returns to the player; the plugin keeps playing.

## Automated (works with the screen locked)

```powershell
& $adb shell am start -n com.vstghost/com.vstghost.PlayerActivity --es load Odin2
Start-Sleep 25
& $adb shell "am start -n com.vstghost/com.vstghost.EditorActivity --ei slot 0 --ei dump 4 --es taps '753,60/20,600'"
Start-Sleep 40
& $adb logcat -d -s VSTGHOST:* | Select-String "editor|underruns"
# Pictures: files/editor-dump-0..4.bmp (0 = editor, 1 = after tapping Osc 2's menu arrow, 2 = menu closed)
cmd /c "`"$adb`" exec-out run-as com.vstghost cat files/editor-dump-1.bmp > dump1.bmp"
```

Expected: `editor_open ... "ok":true`, `protoErrors":0`, dump 1 with 7 mapped surfaces and the
oscillator menu visible, dump 2 back to 2, `underruns 0`.

## PC (no phone)

`tools/wl/vgwl_test.cpp` runs the same compositor on Linux with scripted shots/clicks/drags; build
Wine with `--with-wayland`, then run `vstghost-host.exe editor <plugin>` with
`WAYLAND_DISPLAY=<socket> VSTGHOST_WL_ONE_TOPLEVEL=1` and the prefix's Graphics driver set to `wayland`.
