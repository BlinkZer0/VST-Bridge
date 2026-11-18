# Phase 2: install and test steps (real-time player)

Prerequisites: Phase 0/1 setup; plugins pushed to `Android/data/com.vstghost/files/plugins/` (see phase1-test-steps.md).

## Build and install

```bash
bash payload/build.sh host etc pack play          # Ubuntu/WSL2: host now has `serve` (live mode)
```
```powershell
$env:JAVA_HOME = "C:\Program Files\Android\openjdk\jdk-21.0.8"
.\gradlew.bat assemblePlayDebug
& "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe" install -r app\build\outputs\apk\play\debug\app-play-debug.apk
```

## Play it

Open **VST gHost** → pick a plugin → **Load**. Play the on-screen keyboard (higher on a key = softer). **Stats** shows the buffer, underruns, CPU and a **Latency test**. **Presets** saves and loads the plugin's full state.

From a PC: set the phone's USB mode to **MIDI**. The phone appears on Windows as the MIDI output **"MIDI function"**, and any MIDI app or Web MIDI page (e.g. the HTML MIDI controller) can play the loaded plugin.

## Automated measurements (what the report numbers come from)

Use the **phone speaker** (Bluetooth off): Bluetooth bypasses the low-latency path.
```powershell
$adb = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
& $adb shell am force-stop com.vstghost
& $adb shell am start -n com.vstghost/com.vstghost.MainActivity --ez autorun true --ei phase 2
& $adb logcat -s VSTGHOST:I          # wait for "PHASE2 DONE" (~3 min); don't touch the phone
& $adb pull /sdcard/Android/data/com.vstghost/files/phase2-report.md
```
Also handy: `adb shell am start -n com.vstghost/com.vstghost.PlayerActivity --es load "E-Piano"` loads a plugin directly.
