# Phase 0: install and test steps (Galaxy S26 Ultra)

Everything below runs in **PowerShell on your Windows PC**, except step 1, which runs in Ubuntu (WSL2).

## 0. One-time setup

**WSL2 + Ubuntu** (needs admin and one reboot):
```powershell
wsl --install -d Ubuntu-24.04
```
After the reboot, open "Ubuntu 24.04" from the Start menu once to create your Linux user.

**Phone:** Settings → About phone → Software information → tap *Build number* 7× → Developer options → enable **USB debugging**. Connect by USB, accept the RSA prompt, then:
```powershell
$adb = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
& $adb devices        # should list the phone as "device"
```

## 1. Build the payload (Ubuntu / WSL2, roughly 30–60 min the first time)

*(Already done on the dev PC on 2026-11-18; `payload/out/` exists. Skip to step 2 unless the payload changes.)*

```bash
cd "/mnt/c/Users/Randy/OneDrive/Documents/GitHub/VST Bridge"
bash payload/setup-ubuntu.sh
bash payload/build.sh 2>&1 | tee ~/payload-build.log
```
It ends with `=== done: …/payload/out`. If it fails, send me `~/payload-build.log` (at `\\wsl$\Ubuntu-24.04\home\<user>\payload-build.log` in Explorer).

## 2. Build both APKs (PowerShell, repo root)

```powershell
$env:JAVA_HOME = "C:\Program Files\Android\openjdk\jdk-21.0.8"
.\gradlew.bat assembleSideloadDebug assemblePlayDebug
```
The Play APK bundles the payload, so it's large (≈ payload size + 30 MB).

## 3. Sideload edition (`com.vstghost.sideload`, targetSdk 28)

```powershell
$adb = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
& $adb install -r app\build\outputs\apk\sideload\debug\app-sideload-debug.apk
& $adb push payload\out\vstghost-payload.tar.zst /sdcard/Android/data/com.vstghost.sideload/files/vstghost-payload.tar.zst
& $adb logcat -c
& $adb shell am start -n com.vstghost.sideload/com.vstghost.MainActivity --ez autorun true --ez reinstall true
& $adb logcat -s VSTGHOST:I      # watch progress; Ctrl+C after "PHASE0 DONE" (expect a few minutes)
& $adb pull /sdcard/Android/data/com.vstghost.sideload/files/phase0-report.md phase0-report-sideload.md
& $adb logcat -d | Select-String "avc:|PhantomProcess|SIGSYS|seccomp" > phase0-denials-sideload.txt
```
*If `adb push` to Android/data is refused*, use this instead (it streams into the app's private dir):
```powershell
cmd /c "`"$adb`" exec-in run-as com.vstghost.sideload sh -c 'mkdir -p files/incoming && cat > files/incoming/vstghost-payload.tar.zst' < payload\out\vstghost-payload.tar.zst"
```
Keep the phone unlocked with the app in the foreground while the test runs. The app keeps the screen on.

## 4. Play edition (`com.vstghost`, targetSdk 36, payload inside the APK)

```powershell
& $adb install -r app\build\outputs\apk\play\debug\app-play-debug.apk
& $adb logcat -c
& $adb shell am start -n com.vstghost/com.vstghost.MainActivity --ez autorun true --ez reinstall true
& $adb logcat -s VSTGHOST:I      # Ctrl+C after "PHASE0 DONE"
& $adb pull /sdcard/Android/data/com.vstghost/files/phase0-report.md phase0-report-play.md
& $adb logcat -d | Select-String "avc:|PhantomProcess|SIGSYS|seccomp" > phase0-denials-play.txt
```
I expect the Play edition to fail somewhere: Box64 re-exec'ing itself for `wineserver`, or SELinux exec denials. Finding out where is the point of this test.

## 5. Send back

`phase0-report-sideload.md`, `phase0-report-play.md`, and the two `phase0-denials-*.txt` files. If either report wasn't produced, send `adb logcat -d > logcat.txt` instead.

## If processes get killed

If logcat shows `PhantomProcessRecord` / "Killing" lines for wineserver or box64, re-run after lifting Android's child-process limit (developer setting, reversible):
```powershell
& $adb shell settings put global settings_enable_monitor_phantom_procs false
```
(Revert later with `... true`.) The app will run Wine under a foreground service from Phase 2 onward.

## What gets measured

Each report contains: device/SoC/Android build; payload size (compressed and on disk) and extraction time; `box64 --version`; `wine --version`; **cold** `wine hello.exe` (no wineserver), timed to hello.exe's first output line, its last line and wine's exit; three **warm** runs with a persistent wineserver; process count and peak summed RSS (with per-process peak RSS); and the same DSP benchmark run natively in arm64 and as x86_64 under Box64, giving the overhead factor and a checksum comparison.
