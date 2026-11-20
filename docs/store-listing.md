# Play Store listing — VST gHost

Text and assets for the Play Console listing. Assets made 2026-11-20 from the 1.0.0-beta1 release
build are in `C:\Users\Randy\vstghost-keys\releases\1.0.0-beta1\listing\`:

| File | Console field | Spec |
|---|---|---|
| `icon-512.png` | App icon | 512×512 PNG, the adaptive icon's visible 72 dp area on the app's dark background |
| `feature-graphic-1024x500.png` | Feature graphic | 1024×500 PNG |
| `screenshots/01-player.png` | Phone screenshots | 2160×1080 (2:1, the widest Play accepts); Surge XT loaded, keyboard |
| `screenshots/02-surge-editor.png` | Phone screenshots | Surge XT's own editor running under Wine |
| `screenshots/03-native-editor.png` | Phone screenshots | Brass Knuckles, native build, its editor |
| `screenshots/04-mixer.png` | Phone screenshots | Mixer: four instrument and four effect strips, master |
| `screenshots/05-presets.png` | Phone screenshots | Presets dialog with factory programs |

Regenerate: the icon and feature graphic come from the app's own vector
(`app/src/main/res/drawable/ic_launcher_foreground.xml`) rendered with resvg + Pillow; screenshots
are `adb exec-out screencap` captures (2340×1080) centre-cropped to 2:1 so the long side is at most
twice the short one (Play's rule). Order them in the Console as numbered.

## App name

VST gHost

## Short description (80 characters max)

Run VST3 instruments and effects on your phone, with a keyboard and MIDI in.

## Full description (4000 characters max)

VST gHost runs Windows VST3 plugins on your phone. Load an instrument, play it from the on-screen
keyboard or a USB or Bluetooth MIDI keyboard, add effects, and hear it in real time.

WHAT IT DOES
• Loads 64-bit Windows VST3 instruments and effects you already own or download: zips, .vst3 files
  and installers from your Download folder or an SD card.
• Four instrument slots and a four-slot effect chain, with a mixer (levels, mutes, master) and
  live meters.
• Opens each plugin's own editor window, with touch: knobs, menus and patch browsers work as on a
  PC.
• Presets: factory programs, .vstpreset files, and your own saved sounds.
• Instrument sets: save and recall a whole setup of slots, sounds and mix.
• Keeps playing in the background, with a Stop action in the notification.
• Works as an instrument for other music apps: the app shows up as virtual MIDI devices, and its
  slots are available as Audio Plugins for Android (AAP) in hosts that support them.

BUNDLED INSTRUMENTS
Five instruments are built in and ready to play: E-Piano, Brass Knuckles, No Strings, The Ham and
the Pedal Faster effect. Nothing to download.

HOW IT WORKS
Windows plugins run inside a built-in Wine runtime with x86-64 emulation, so the plugin's own code
and interface run unchanged. The runtime unpacks once at first start (about a minute) and again
after updates. Plugins that need their installer can be installed from the installer itself.

WHAT TO EXPECT
• Plugins written for a PC vary in how much CPU they need. Simple synths play dozens of voices;
  heavy ones may need a bigger buffer. The app shows load time and audio statistics for each slot.
• Plugins with copy protection that phones home, or that need a 32-bit host, may not run.
• Plugin editors are drawn in software; large editors can be slow to open the first time.
• A compatibility list of tested free plugins is maintained with the app.

PERMISSIONS
• All files access: only to list and open the plugin files you choose from Download or an SD
  card. The app reads the file you tap and nothing else.
• Notifications: the "VST gHost is playing" notification while a plugin is loaded.
No account, no ads, no analytics, nothing leaves the phone.

VST is a trademark of Steinberg Media Technologies GmbH. Plugins are the property of their
respective authors and are not included unless listed above.

## Release notes (first release)

First release. Runs Windows VST3 instruments and effects with their own editors, five bundled
instruments, four instrument slots plus a four-slot effect chain, mixer, presets, instrument sets,
background playing, virtual MIDI devices and AAP plugin slots.

## Other fields

- Category: Music & Audio. Tags: synthesizer, VST, MIDI, music production.
- Contact email: the developer address of the Play account (not written here).
- Privacy policy URL: where `docs/privacy-policy.md` is hosted (see play-console-answers.md).
- Content rating, data safety, permission declarations: `play-console-answers.md`.
