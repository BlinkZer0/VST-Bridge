# Phase 3: test steps (AAP plugins + virtual MIDI devices)

Prerequisites: Phase 2 setup; plugins pushed to `Android/data/com.vstghost/files/plugins/`.

## Build and install

```powershell
$env:JAVA_HOME = "C:\Program Files\Android\openjdk\jdk-21.0.8"
.\gradlew.bat assemblePlayDebug          # AGP 9.5 / Gradle 9.7 / compileSdk 37 (Android SDK platform 37 needed)
$adb = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
& $adb install -r app\build\outputs\apk\play\debug\app-play-debug.apk
```

## Assign VSTs to slots

In the app: **Slots** → pick a slot → pick a plugin. Or from the PC:
```powershell
& $adb shell am start -n com.vstghost/com.vstghost.PlayerActivity --es assign "'1=E-Piano;2=Odin2;5=Pedal Faster'"
```

## AAP host test (aap-juce-simple-host)

Install the host from https://github.com/atsushieno/aap-juce-simple-host/releases (v0.9.0, `app-debug.apk`). Then, in portrait (that's where it shows its keyboard):
1. **Scan plugins** → first dropdown **VST gHost** → second dropdown **VST gHost Inst 1** → **Add** (first load takes about 1.5 s).
2. Play the keyboard at the bottom.
3. Watch it working: `& $adb logcat -s VSTGHOST:I` shows `AAP slot 1: … MIDI msgs, output peak … dBFS` every 3 s.

## Virtual MIDI device test

- **Automated** (opens the devices through Android's MIDI API and plays Inst 1 and Inst 2 together):
  ```powershell
  & $adb shell am start -n com.vstghost/com.vstghost.MainActivity --ez autorun true --ei phase 3
  & $adb pull /sdcard/Android/data/com.vstghost/files/phase3-report.md
  ```
- **With any MIDI app:** choose the Android MIDI output **"VST gHost Inst 1"** and play. Sound comes out of VST gHost's audio output. The slot's Windows host starts on the first note (about 2 s).
