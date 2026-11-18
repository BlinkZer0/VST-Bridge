# Phase 1: install and test steps

Prerequisites: Phase 0 setup (WSL2 Ubuntu, USB debugging). PowerShell on the PC, repo root.

## 1. Build

```bash
# Ubuntu (WSL2), repo root. `host` builds vstghost-host.exe + tests/phase1.mid.
bash payload/build.sh prefix host etc pack play
```
```powershell
$env:JAVA_HOME = "C:\Program Files\Android\openjdk\jdk-21.0.8"
.\gradlew.bat assembleSideloadDebug assemblePlayDebug
```

## 2. Install, push plugins, run (Play edition shown; for sideload use `com.vstghost.sideload` and also push the payload as in Phase 0)

```powershell
$adb = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
$pkg = "com.vstghost"
& $adb install -r app\build\outputs\apk\play\debug\app-play-debug.apk
# Create the folder FIRST: pushing a folder into a missing destination makes it the destination.
& $adb shell mkdir -p /sdcard/Android/data/$pkg/files/plugins
foreach ($p in "E-Piano.vst3","Brass Knuckles.vst3","Pedal Faster.vst3","Odin2.vst3") {
  & $adb push "$env:USERPROFILE\Downloads\$p" /sdcard/Android/data/$pkg/files/plugins/
}
& $adb shell am force-stop $pkg
& $adb shell am start -n $pkg/com.vstghost.MainActivity --ez autorun true --ei phase 1 --ez reinstall true
& $adb logcat -s VSTGHOST:I        # Ctrl+C after "PHASE1 DONE" (about 35 s)
& $adb pull /sdcard/Android/data/$pkg/files/phase1-report.md phase1-report.md
```
Optional: `--ei block 128` renders with 128-frame blocks.

## 3. Get the WAVs

The app's copies are private files, so stream them out:
```powershell
New-Item -ItemType Directory -Force results-audio\phase1 | Out-Null
foreach ($p in "Brass Knuckles","E-Piano","Odin2","Pedal Faster") {
  cmd /c "`"$adb`" exec-out run-as $pkg cat `"files/phase1-out/$p.wav`" > `"results-audio\phase1\$p.wav`""
}
```

## 4. Compare with the PC reference (optional)

Render the same files natively on the PC with the same host, Wine and prefix (see `docs/phase1-report.md`, "Setup"), then:
```bash
python3 tools/compare_wav.py <pc-reference.wav> "results-audio/phase1/<name>.wav"
```
