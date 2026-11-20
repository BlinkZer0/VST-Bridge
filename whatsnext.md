# What's next — VST gHost (as of 2026-11-19, branch `phase-5`)

Phase 5 state: **committed and verified on the phone** (see `docs/phase5-report.md`,
`docs/phase5-test-steps.md`, STATUS.md). Payload series 2 = Wine 11.0 + patches 0001–0004,
Box64 0.4.4 (AVX off), Mesa 25.0.7 llvmpipe, Termux glibc 2.44; APK `app-play-debug.apk` built
with `-Pvg.nativePlugins=epiano;brass;pedalfaster`.

Verified today on the final build: Dexed editor complete + taps (12.9 s open, ≈1.8 cores while
open), Surge XT engine + OpenGL editor (2.2 s, 0.1 core), Pedal Faster Windows build (1.7 s,
0.55 core); 0–2 audio underruns with editors open.

## Remaining Phase 5 items

1. **Odin2 native build** — JUCE hard-codes `JUCE_MODAL_LOOPS_PERMITTED 0` on Android; Odin2 uses
   `PopupMenu::show()` / `AlertWindow::showOkCancelBox()`; also `-fbracket-depth` for its 258-param
   layout. Options: patch JUCE at fetch time + nested `Looper.loop()` for modal loops, or leave Odin2
   on the Wine path (works) and drop it from the default native list.
2. ~~Corpus on the phone~~ — done 2026-11-19 (see item 4 below): all 30 load and run, 25/25 GUIs open,
   including Wolf Shaper, Dragonfly, Surge XT Effects and Monique.
3. **Sideload edition**: build `assembleSideloadDebug`, push the payload, repeat the Dexed/Surge
   checks (patch 0004 is harmless there).
4. **Installer-only plugins** (Surge XT's factory data lives in `%ProgramData%\Surge XT` from its
   installer; without it the editor works but has no factory patches/wavetables).
5. `.jucer` (Projucer) projects in the adapter; clear "unsupported plugin" messages.
6. Direct2D editors still cost ≈2 cores (every presented frame is software-rasterized); ideas: present
   only the dirty box to GDI, lower `VSTGHOST_VBLANK_MS` further, newer Box64 (AVX translation).
7. Merge/PR `phase-5` → `main` once the user has used the editors on screen.

## TODO from the user (2026-11-19) — next after Phase 5

1. **Separate FX and Instrument sections — DONE 2026-11-19.** The player shows Inst 1–4 and FX 1–4
   (the same slots AAP hosts and the virtual MIDI devices use); instruments sum onto a bus, the
   effects form a serial chain on it (`engine.cpp`), "FX input" switches the chain's source
   between the instruments and the test signal. Verified: E-Piano → Pedal Faster (native),
   Dexed → Pedal Faster → Surge XT Effects (Wine in both roles), 0 underruns. Not yet: mic/line-in
   as FX input, per-slot bypass/levels, drag-to-reorder the chain.
2. **Beautify the UI — first pass DONE 2026-11-19** (Material 3 dark theme, amber accent; plugin
   picker, slot cards, one control row, overflow menu, Material dialogs). Not yet restyled: the
   Tests screen, EditorActivity/NativeEditorActivity chrome (Fit/Close bar), the parameter rows
   (plain SeekBars), an app icon, portrait layout.
3. **Stock presets — DONE 2026-11-19.** Presets dialog = factory programs (VST3 program lists /
   JUCE programs, current one marked) + `.vstpreset` files (bundle `Resources/Presets`, the prefix's
   `VST3 Presets` folders, `files/presets/<plugin>/`) + saved presets; saving a bridged plugin's sound
   writes a `.vstpreset`. Verified with Dexed (32 programs; save/load round-trip restores the
   parameters) and Brass Knuckles (native programs). Known: the "current program" dot can lag after
   a state restore (JUCE's program parameter); plugins without a program list (Surge XT) only get
   `.vstpreset`/saved presets — Surge's own `.fxp` library would need its data folder (item 4 of the
   Phase 5 list). **Caveat found:** the Wine prefix is replaced on every app update, so anything a
   plugin stores inside it (Surge's user data, plugin-installed presets) is lost — persist
   `prefix/drive_c/users/vstghost` across payload reinstalls.
4. **More free VST3s for compatibility — DONE 2026-11-19 (phone pass included).** `tools/corpus/` (manifest, fetch.sh,
   pc-triage.sh, phone-triage.sh) is committed; the corpus is 30 plugins (Dexed, Surge XT + FX,
   Monique, Dragonfly ×4, Wolf Shaper, Uhhyou ×3, DPF-Plugins ×15, ADLplug, OPNplug, Stochas).
   PC: all 30 load and render; 24 editors open (the DPF "Ma*" ports and Kars have no GUI;
   DragonflyEarlyReflections' editor run gave no output — check). Most other free plugins
   (OB-Xd, Odin2, TAL, Helm, NAM, Chow*, u-he, sfizz) ship only installers → the installer-import
   item is what unlocks them. Phone pass done: 30/30 load and run, 25/25 GUIs open, 0 underruns;
   `docs/compatibility.md` has both columns. The adb self-test now feeds effects the FX-chain test
   signal (they sat on a silent instrument bus before). Script fixes: tab-safe names, no stale editor
   picture for GUI-less plugins, runs from Git Bash (`MSYS_NO_PATHCONV`, `pwd -W`); corpus copy for
   Windows adb at `C:/Users/Randy/vstghost-corpus/x`; pin `ANDROID_SERIAL` when a second device shows up.

5. **Install plugin from the phone's folders — DONE 2026-11-19.** ⋮ → "Install plugin…" scans Download,
   Documents, the storage root and SD cards for `.vst3` files/bundles and zips (installers and `.7z`
   are listed but marked not installable) and installs the tapped one; needs "All files access"
   (`MANAGE_EXTERNAL_STORAGE`; the app explains and opens the settings page once). Verified with
   the Dexed zip and the No Strings / The Ham bundles. Note for Play: the All-files-access
   permission needs a declaration in the Play Console (core use: installing plugin files the user
   downloaded); the alternative is the existing SAF picker ("Add"), which needs no permission.
6. **No Strings and The Ham native — DONE 2026-11-19.** Both are in `native/CMakeLists.txt` as
   local-only projects (no public archive yet: No Strings has no remote, The Ham's GitHub archive is
   not reachable), so the build needs `-Pvg.pluginSrcRoot=C:/Users/Randy/OneDrive/Documents/GitHub`;
   without it they are skipped with a message. Verified: load 4–8 ms, 53 / 143 parameters, web UI
   editors complete, preset arrows work. Two fixes on the way: the adapter no longer shrinks an
   editor below its logical size (web pages clipped; the zoom layout scales the view instead), and
   The Ham's `vst/ui/index.html` viewport meta now locks the scale like E-Piano's does (Android
   WebView inflated its layout viewport to the overflowing page, so its fit() computed the wrong
   scale), and No Strings' `vst/ui/js/app.js` fit() no longer switches to the compact phone layout
   inside the plugin editor (touch pointer + height-limited window triggered it; its desktop page is
   1180×1182, taller than the 1180×900 editor, so it shows with side gaps — a taller editor size on
   Android would fill the view). Both plugin-repo changes are **uncommitted there**. To make the
   build reproducible again: push both repos and pin archive URL + SHA-256 in `native/CMakeLists.txt`.
   Background (Windows builds): Windows builds install, load and
   play (55 / 145 parameters, 0.6 s load) but their editors stay blank — they are WebView2
   (Edge/Chromium) web UIs (`NEEDS_WEBVIEW2` in their CMake), and JUCE's fallback is the IE
   `mshtml` view, which the prefix does not have either (no wine-gecko, and the UI needs a modern
   engine anyway). Same story as the E-Piano / Brass Knuckles Windows twins. Fix = native builds
   through the JUCE adapter (`-Pvg.nativePlugins=…;nostrings;theham`), whose editors already show
   the web UI in an Android WebView (E-Piano). Their repos: `GitHub/No Strings/vst`,
   `GitHub/The Ham/vst` (JUCE fetched by CMake, tag `8.0.17-security`).

7. **Mixer, Uninstall, app icon — DONE 2026-11-19.** ⋮ → Mixer: eight strips with live meters
   (blue→teal→amber→red, peak hold), faders with a mixer taper (0 dB at ¾, −∞..+6 dB) and mutes;
   the engine applies a per-slot gain in the audio callback and keeps a per-slot peak the mixer
   polls at 30 Hz; faders/mutes persist (SharedPreferences "mixer") and are re-applied at start.
   ⋮ → Uninstall plugin…: imported Windows plugins only (native builds are part of the APK), unloads
   the plugin from any slot, deletes its files, keeps `files/presets`. Icon: ghost with headphones
   as an adaptive vector icon (`drawable/ic_launcher_foreground.xml`, dark background, monochrome
   layer). Not yet: the mixer as a persistent panel instead of a dialog, pan/solo, a master strip,
   meters on the slot cards, saving the mix with presets.

8. **Background playing — DONE 2026-11-19 (verified: notification, survives leaving the screen, Stop unloads all).** `PlaybackService`: a
   media-playback foreground service with a "VST gHost is playing" notification (Stop action
   unloads every slot) and a partial wake lock; `Bridge` starts/updates it after every load and
   stops it after the last unload, so virtual-MIDI and AAP loads are covered too. Android refuses a
   foreground start from an app that is neither visible nor bound by a visible one (seen when a load
   came in with the screen off): then it stays a plain service and the next load retries. Asks for
   POST_NOTIFICATIONS once (Android 13+).
9. **Prefix data survives app updates — DONE 2026-11-19.** `Payload.installLocked` moves
   `drive_c/users`, `ProgramData`, `Program Files/Common Files/VST3` (+ `VST3 Presets`) aside
   before the old tree is deleted and merges them into the fresh prefix. Verified: a marker file in
   the user folder survived a reinstall.
10. **Meters on the slot cards + master strip — DONE 2026-11-19 (verified).** A thin bar under
   each slot button (same colours, peak hold) fed by one 30 Hz poller in the player, which also
   feeds the mixer dialog (the engine's meters reset on read, so there is exactly one reader); the
   mixer gained a Master strip (engine output peak, master gain). The mix is persisted with the
   other mixer settings; saving it inside plugin presets was not done (presets are per plugin).
11. **Installer-only plugins — code DONE, untested.** Install plugin… now accepts .exe/.msi: the
   installer is copied into the prefix's tmp and run unattended under Wine (Inno Setup
   `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /NOCANCEL`, NSIS `/S`, MSI `msiexec /qn`; 10 min
   limit), then any new `.vst3` in `Common Files\VST3` is moved into files/plugins; the installer's
   data (ProgramData etc.) stays in the prefix (item 9 keeps it). Not yet run: the safety tooling
   here refused to execute a downloaded installer on the PC — try it from the phone with the Surge
   XT setup (`surge-xt-win64-…-setup.exe`, Inno Setup, 200 MB; it also brings Surge's factory
   data). Installers that need a window will time out; .NET-based ones will not run.
12. **Play readiness.** Sideload edition rebuilt (`assembleSideloadDebug`), installed with its
   payload (external files dir) — Dexed (editor 12 s) and Surge XT (editor 2.3 s) verified. The Ham's viewport fix is
   pushed (`03c53cd`); No Strings' layout fix is committed locally — the repo has no remote yet.
   The All-files-access declaration for the Play Console (core use case: installing plugin files the
   user downloaded; no cloud/backup use) still has to be filled in there.

13. **Play together, wet FX faders, instrument sets — DONE 2026-11-19.** Long-press a loaded
   instrument → "Play together": the keyboard and USB MIDI then drive every slot in the group
   (`NativeEngine.playerSlots`); green = in the group, orange outline = current slot. FX faders are
   now wet/dry (engine mixes input and output per effect; 100 % = fully processed, mute = bypass); the
   FX chain was already serial in slot order. ⋮ → Instrument sets…: `files/sets/<name>.json` with
   each loaded slot's plugin + `get_state`, the play group and the mixer; Load unloads everything,
   loads in order and restores state. Not yet: auto-load the last set at start, export/import sets.
14. **Sideload edition retired** as a target (Play Store only). The flavour still builds for
   troubleshooting; no further sideload testing.

15. **Release batch (2026-11-19, order: 13, 14, 2, 3, 4+TOS, 5, 6, 7, 8, 9, 11, 12, then 1).**
   - 13 Installer test — DONE 2026-11-20 with **WoW64 (payload series 3)**. Inno Setup / NSIS
     launchers are 32-bit x86 programs (surge-xt-setup.exe, Native-Access_2.exe: PE machine 0x14c);
     the series-2 runtime (x86_64 only) exited 1 at once. Now: Wine built `--enable-archs=i386,x86_64`
     (i686 MinGW in setup-ubuntu.sh; +49 MB payload: 133 MB packed), Box64 unchanged except linked
     at 0x4000000000 and reserving 0x110000-0x68000000 of low memory for Wine (its old 0x34800000
     image capped 32-bit programs at ~750 MB; Surge's installer commits 1 GB for its LZMA
     dictionary), and ntdll patched to treat unix file names as UTF-8 (patch 0005; Inno failed
     renaming "µcomputer.fxp"). Install plugin now collects bundles from vendor subfolders
     ("Surge Synth Team"), counts re-installs as new, passes /LOG to Inno and shows its tail on
     failure; files/installer.log + installer-inno.log go into problem reports; files/installer.env
     (KEY=VALUE) overrides the installer's environment for experiments over adb.
     Verified on the phone: surge-xt-setup.exe → "Installed Surge XT, Surge XT Effects", 497 MB of
     factory data in ProgramData, editor's patch browser steps through the factory patches.
     The app refuses 32-bit installers only when the runtime has no i386 side.
     Corpus regression pass on series 3 (2026-11-20, `results-phone-s3`): 30/30 import and load,
     0 underruns, 24/25 editors open (median 1.36 s vs 1.40 s before; render 188 µs vs 180 µs).
     The one editor failure was a version clash, not the runtime: the corpus zip's Surge XT 1.3.4
     overwrote the installer's 1.4.0 plugin while the 1.4.0 data stayed in ProgramData, and 1.3.4
     crashes (0xC0000005) opening its editor on that data. Re-running the installer restored 1.4.0
     (editor 2.1 s). Rough edge: a plugin and its ProgramData must match; importing an older zip
     over an installer-installed plugin is not detected.
   - 14 Odin2 native: builds and its modal menus work (nested Looper, `JuceModalLoop.kt` +
     `vgn_adapter.cpp`), but Odin2 is GPL-3 and the app is proprietary → not bundled. `.jucer`
     projects: `VgJucer.cmake` + `JUCER` option of `vg_add_juce_project` (test entry `dexedjucer`,
     TEST_ONLY): the reader handles project attributes, sources, binary data, JUCE and user modules
     (MODULEPATH), JUCEOPTIONS and the exporter's include paths/defines. With Dexed 0.9.6 it reaches
     compilation; what fails there is Dexed's JUCE-6 API use (MidiInput::getDevices, parameter
     gestures), i.e. a .jucer project needs JUCE-9-compatible code like any CMake project.
     No Strings still has no git remote (create one; then I push and pin).
   - 2 16 KB pages: verified (ELF segments 16/64 KB, zipalign -P 16 passes). Nothing to do.
   - 3 About screen (⋮ → About): version, Terms, Privacy, Notices, every license text from assets.
   - 4 `docs/privacy-policy.md`, `docs/terms-of-service.md` (also in the app), `docs/play-console-answers.md`.
   - 5 glibc from source: `payload/build-glibc-src.sh` (setup as root → tools → build). Setup on
     Ubuntu 24.04 needed the termux script's "resolute" names translated; rerun `wsl -u root bash
     payload/build-glibc-src.sh setup`, then `tools` and `build` as yourself (long).
   - 6 Setup progress dialog with a bar while the runtime unpacks; Retry / Report on failure.
   - 7 First-run card (once; pref `onboarded`).
   - 8 ⋮ → Report a problem…: zip (versions, device, plugins, slots, host logs, logcat, sets, prefs)
     via FileProvider + share sheet.
   - 9 Audio focus (PlaybackService): mute on loss, duck to 25 %, restore on gain; notes released.
     After a permanent loss the next note asks for focus again (Android never hands it back by
     itself). Verified on the phone 2026-11-19: a podcast app started over a held note → loss
     logged, note released; after pausing it, one key press put VST gHost back on the focus stack.
     Route changes were already handled (Oboe reopen).
   - 11 ⋮ → MIDI settings…: input channel filter, route-by-channel (ch N → Inst N), velocity curve.
   - 12 Editor power: while EditorActivity is paused (screen off / other app) the Wine window is
     hidden at once (`editor_visible` host command) and, after 30 s in the background, the editor is
     closed; it is reopened on resume (Dexed: 39 ms, state kept). Measured on Dexed: host ~185 %
     CPU with the editor open, ~183 % hidden (plugins keep repainting their own buffers on timers),
     3.5 % closed — so the close is what saves the battery.
   - Phone checks done 2026-11-19 for 3 (About), 6 (Setting up dialog with bar), 7 (first-run card),
     8 (report zip → share sheet), 9 (audio focus, see above), 11 (MIDI settings dialog), 12 (numbers above).
   - 1 Release build — DONE: `bundlePlayRelease` → 127 MB signed bundle (upload key at
     `C:/Users/Randy/vstghost-keys/upload.jks`, password in the git-ignored `keystore.properties`),
     payload as the install-time asset pack `payload`, R8 with `app/proguard-rules.pro`, mapping.txt.
     **Your check:** `java -jar bundletool.jar build-apks --bundle=app/build/outputs/bundle/playRelease/app-play-release.aab --output=t.apks --mode=universal`,
     unzip `t.apks`, then `unzip -v universal.apk | grep payload` must say `Stored` (the app opens the
     archive by file descriptor; a deflated entry would break first-run setup) — then install
     universal.apk and run through docs/release.md's checklist. The tooling here would not run the jar.

## Known rough edges (not blockers)

- After unloading an OpenGL or Direct2D-emulated plugin, the host process does not exit on its own
  (it hangs in teardown under llvmpipe until its Wayland connection drops); the app kills it after
  3 s (`Bridge.stop`). Seen on the PC with Dexed, ProM, glBars, DragonflyEarlyReflections, wolf-shaper.
- `docs/compatibility.md` has the PC and phone columns for the 30-plugin corpus.
- The phone's screen times out during long waits: `adb shell svc power stayon usb` keeps it on, but a PIN
  lock still needs the user. A load that arrives while the app is not visible gets its foreground
  service start refused (logged, harmless). `--es importZip` is read only when the activity is created
  (force-stop first); `--es load` works on a running instance.
- Changing an app op over adb (`appops set … MANAGE_EXTERNAL_STORAGE`) kills the app; relaunch before
  driving the UI. `adb push` from the OneDrive desktop folder hung; stage files locally first.

## Operational notes

- Reinstalling the APK re-extracts the payload (~20 s when undisturbed; a force-stop during it
  leaves the old tree and re-extracts on the next start). Hand-pushed files get overwritten.
- Debug: `files/env.debug` (KEY=VALUE, wins over the app's env), `VSTGHOST_DXGI_DUMP`,
  `tools/phone/*`.
