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

Before uploading, bump `versionCode` (and `versionName`) in `app/build.gradle.kts`: Play needs a
higher versionCode every time.

## What the bundle contains

- Base module: the app, the native plugins (`libvgn_*.so`), the Wine loader/Box64/glibc libraries
  in `lib/arm64-v8a/`, resources, the license texts and legal documents (assets).
- `payload` asset pack (install-time): `vstghost-payload.tar.zst` from `payload/out/play-assets`.
  It is delivered with the app and read through the normal AssetManager.

## Checks before upload

- `bundletool build-apks --bundle app-play-release.aab --output t.apks --mode=universal` and install
  the universal APK on the phone: first start must show "Setting up" and finish; load a bundled
  instrument; open its editor; import a zip; play from a MIDI keyboard.
- Native libraries 16 KB aligned (already true: `llvm-readelf -l` shows 0x4000/0x10000 LOAD alignment).
- The licenses screen (⋮ → About) opens and lists every component.
- Play Console: data safety, permission declarations and listing texts in
  [play-console-answers.md](play-console-answers.md); privacy policy URL live.
