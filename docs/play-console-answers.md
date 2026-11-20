# Play Console — answers for VST gHost

What to enter in the Play Console forms, matching what the app actually does (2026-11-19).

## Data safety

- **Does your app collect or share any of the required user data types?** No.
- **Is all of the user data collected by your app encrypted in transit?** Not applicable (no data).
- **Do you provide a way for users to request that their data is deleted?** Not applicable; the app
  stores nothing off the device. (Uninstalling the app removes its private storage.)
- **Data types:** none collected, none shared.
- **Security practices:** no data in transit.

## Permissions declarations

### All files access (`MANAGE_EXTERNAL_STORAGE`)

- **Core functionality:** file management / plugin installation. The app's purpose is to run plugin
  files the user obtains elsewhere (downloads from plugin vendors' sites, files on an SD card). The
  "Install plugin…" screen lists `.vst3`, `.zip`, `.exe` and `.msi` files in Download, Documents and
  on SD cards and copies the one the user taps into the app's private storage.
- **Why the Storage Access Framework is not enough:** the picker is offered too ("Add"), but it
  cannot show the user *which* of their downloads are plugins, cannot pick a `.vst3` bundle folder
  with its contents in one step on all devices, and vendors' zips land in Download where users
  expect the app to find them.
- **Scope:** the app reads only the file the user taps. It never scans, indexes, modifies or uploads
  other files, and keeps no index of the user's storage.
- **Video for review:** record ⋮ → Install plugin… → permission dialog → Android settings toggle →
  back in the app the list of files → tapping one → the plugin appears in the plugin list.
  Recorded 2026-11-20 on the 1.0.0-beta1 release build (77 s, 1080p):
  `C:\Users\Randy\vstghost-keys\releases\1.0.0-beta1\review-videos\review-all-files-access.mp4`.
  It shows the dialog ("Allow access to your files" → Open settings), the Android "All files
  access" page with the toggle turned on, ⋮ → Install plugin… again listing the Download folder
  (zips, .vst3, an installer), tapping `Surge XT.zip`, "Imported Surge XT" and Surge XT in the
  plugin picker.

### Foreground service (`FOREGROUND_SERVICE_MEDIA_PLAYBACK`)

- **Type:** media playback. The service runs while a plugin is loaded so the audio engine keeps
  running when the user switches apps (other music apps send MIDI to the app's virtual MIDI devices,
  and AAP hosts use it as a plugin). It shows the "VST gHost is playing" notification with a Stop
  action and stops itself when the last plugin is unloaded.
- **Video for review:** load a plugin → press Home → the notification is visible → play from a MIDI
  app or return and press Stop.
  Recorded 2026-11-20 on the 1.0.0-beta1 release build (71 s, 1080p):
  `C:\Users\Randy\vstghost-keys\releases\1.0.0-beta1\review-videos\review-foreground-service.mp4`.
  It shows Load → Inst 1 (Surge XT), two keys played, Home, the shade with "VST gHost is playing ·
  Inst 1: Surge XT" as the only notification, the notification expanded, Stop tapped, the shade
  reading "No notifications", and the app reopened with Inst 1 empty. The shade shows every
  notification on the phone, so clear the others before a retake.

## Store listing

- **Category:** Music & Audio.
- **Short description (80 chars):** Run VST3 instruments and effects on your phone, with a keyboard
  and MIDI in.
- **Content rating questionnaire:** no violence, no user-generated content, no ads, no purchases.
- **Privacy policy URL:** host `docs/privacy-policy.md` on any public web page you control (the
  repository is private, so not GitHub Pages of it) and enter that address. The terms are in
  `docs/terms-of-service.md`; both are also shown in the app under ⋮ → About.
- **Pricing:** paid app. Decide the price and whether to offer a trial; a paid listing also needs
  the merchant account set up in the Console.
- **Target audience:** 18 and over (simplest; the app has no child-directed content).
- **Ads:** no.
- **App access:** all functionality is available without a login.

## Recording the review videos

Phone screen recorder over adb, app driven with `input tap` (coordinates from `uiautomator dump`),
landscape, release build installed from the universal APK:

```
adb shell svc power stayon usb                      # the screen must not time out mid-take
adb shell screenrecord --time-limit 160 --bit-rate 6000000 /sdcard/review-<name>.mp4 &
…drive the UI…
adb shell pkill -INT screenrecord; adb pull /sdcard/review-<name>.mp4
```

Before the All-files-access take: ⋮ → Uninstall plugin… removes the plugin to be imported again,
`adb shell appops set --uid com.vstghost MANAGE_EXTERNAL_STORAGE default` (the uid-level mode is
the one that counts) and a force-stop put the permission back to "not granted". A zip of a
plugin in `Download` is what the list shows. Play wants under 2 minutes per video; keep takes
short and do not show other apps' notifications.

## Technical requirements (checked 2026-11-19)

- Target SDK 36; min SDK 29.
- 16 KB page size: every `.so` in the APK is 16 KB or 64 KB aligned (`llvm-readelf -l`), and
  `zipalign -c -P 16 -v 4` passes.
- Size: the debug APK is 184 MB; the release must ship as an app bundle with the Wine payload
  (84 MB) in an install-time asset pack (see the release-build item in `whatsnext.md`).
