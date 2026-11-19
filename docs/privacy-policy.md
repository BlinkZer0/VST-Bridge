# VST gHost — Privacy Policy

_Effective 2026-11-19_

VST gHost is a music app that runs VST3 instrument and effect plugins on your Android device. This
policy explains what the app does with data. The short version: **the app does not collect, store
or transmit any personal data, and it has no server.**

## What the app does not do

- It does not create an account, and it does not ask for your name, e-mail address or any identifier.
- It does not send anything over the network. The app has no analytics, no crash reporting service,
  no advertising and no telemetry. (The only network permission the app declares is none.)
- It does not read your contacts, location, camera, microphone, messages or calendar.
- It does not share data with third parties, because it has none to share.

## What stays on your device

- **Plugin files you install** (`.vst3` files, zips and installers you choose from your Download or
  Documents folder, an SD card, or the file picker) are copied into the app's private storage so the
  app can run them. Uninstalling a plugin from the app deletes that copy.
- **Plugin settings, presets and instrument sets** you save are kept in the app's private storage.
- **Mixer and player settings** (faders, mutes, the play-together group, the last selected plugin)
  are kept in the app's private preferences.
- A plugin you run may itself write settings or presets; those are kept inside the app's private
  Windows-compatibility folder (the "prefix") and survive app updates.

All of this lives in the app's private area and is removed when you uninstall VST gHost. None of it
leaves the device.

## Permissions and why

| Permission | Why |
|---|---|
| All files access (`MANAGE_EXTERNAL_STORAGE`) | So **Install plugin…** can list plugin files you downloaded (Download, Documents, SD cards) and copy them into the app. The app only reads files you tap. It never scans, indexes or uploads your files. You can decline and use the **Add** file picker instead. |
| Foreground service, notifications | To keep playing while the app is in the background (the "VST gHost is playing" notification with its Stop button). |
| Wake lock | So audio does not stop when the screen turns off while a plugin is loaded. |
| MIDI | To receive notes from USB or Bluetooth MIDI keyboards and from other music apps. |

## Third-party plugins

Plugins you install are third-party software running inside the app. VST gHost does not alter what
they do. A plugin could, in principle, keep its own settings or license files; it runs without
network access, in the app's private storage, under the permissions listed above.

## Children

The app is not directed at children and collects no data from anyone.

## Changes

If this policy changes, the new version is published at the same address with a new effective date.

## Contact

Questions about this policy: use the contact address shown on the app's Play Store listing.
