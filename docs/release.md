# Release build (Google Play)

## Once: the upload key

_Done 2026-11-19: `C:/Users/Randy/vstghost-keys/upload.jks` (alias `upload`, RSA 4096, PKCS12) and the
repository's git-ignored `keystore.properties` with its generated password. Back both up; they are
outside git on purpose. To make a new one:_

```powershell
# a keystore outside the repository; keep it and the passwords safe (losing it = new app listing)
& "C:\Program Files\Android\openjdk\jdk-21.0.8\bin\keytool.exe" -genkeypair -v -keystore C:\Users\Randy\vstghost-keys\upload.jks `
    -alias upload -keyalg RSA -keysize 4096 -validity 36500 -storetype PKCS12
```

Then `keystore.properties` in the repository root (git-ignored):

```
storeFile=C:/Users/Randy/vstghost-keys/upload.jks
storePassword=…
keyAlias=upload
keyPassword=…
```

Without the file, release builds sign with the debug key (installable, not uploadable).

## Every release

```powershell
$env:JAVA_HOME = "C:\Program Files\Android\openjdk\jdk-21.0.8"
bash payload/build.sh                 # only when the payload changed (WSL; writes payload/out)
.\gradlew.bat bundlePlayRelease "-Pvg.nativePlugins=epiano;brass;pedalfaster;nostrings;theham" "-Pvg.pluginSrcRoot=C:/Users/Randy/OneDrive/Documents/GitHub"
```

Output: `app/build/outputs/bundle/playRelease/app-play-release.aab` (upload this) and
`app/build/outputs/mapping/playRelease/mapping.txt` (upload with it, for readable crash reports).
Copy both, with `payload/out/vstghost-payload.tar.zst.sha256` and the stage's `manifest.json`, to
`C:\Users\Randy\vstghost-keys\releases\<versionName>\` (outside the repository, next to the key),
and tag the commit `v<versionName>`.

A universal APK of the bundle, for the checks below, comes from the Android Gradle Plugin's own
copy of bundletool (no download needed):

```powershell
.\gradlew.bat :app:packagePlayReleaseUniversalApk "-Pvg.nativePlugins=epiano;brass;pedalfaster;nostrings;theham" "-Pvg.pluginSrcRoot=C:/Users/Randy/OneDrive/Documents/GitHub"
```

→ `app/build/outputs/apk_from_bundle/playRelease/app-play-release-universal.apk`. `unzip -v` on it
must list `assets/vstghost-payload.tar.zst` as `Stored` (the app opens the archive by file
descriptor; a deflated entry would break first-run setup), and `apksigner verify --print-certs`
must name the upload certificate (`CN=VST gHost, O=Blink Zero`).

Before uploading, bump `versionCode` (and `versionName`) in `app/build.gradle.kts`: Play needs a
higher versionCode every time.

## What the bundle contains

- Base module: the app, the native plugins (`libvgn_*.so`), the Wine loader/Box64/glibc libraries
  in `lib/arm64-v8a/`, resources, the license texts and legal documents (assets).
- `payload` asset pack (install-time): `vstghost-payload.tar.zst` from `payload/out/play-assets`.
  It is delivered with the app and read through the normal AssetManager.

## Checks before upload

- Install the universal APK (above) on the phone after uninstalling any debug build (different
  key): first start must show "Setting up" and finish; load a bundled instrument; open its editor;
  import a zip; play from a MIDI keyboard. Done for 1.0.0-beta1 on 2026-11-20 (all but the MIDI
  keyboard: none was attached).
- Native libraries 16 KB aligned (already true: `llvm-readelf -l` shows 0x4000/0x10000 LOAD alignment).
- The licenses screen (⋮ → About) opens and lists every component.
- Play Console: data safety, permission declarations and listing texts in
  [play-console-answers.md](play-console-answers.md); privacy policy URL live.
