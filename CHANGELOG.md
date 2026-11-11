# Changelog

## Unreleased

- **Performance Overlay.** The FPS Counter row becomes Performance Overlay with
  three values: off, the game's frame rate with the presentation rate beside
  it, or frame times as well (the window's mean, median, 95th percentile and
  worst frame). The game's rate comes from its own world timer, one interval
  per finished frame. The INI key stays `fps_counter`, so a saved 0 or 1 keeps
  its meaning.
- **Black Box.** BLACK keeps the last minute of the controller state the game
  actually saw. Controls → Advanced Gamepad → Controller Diagnostics → Save
  Black Box writes it beside the settings as `black-box-<stamp>.csv`, headed by
  a copy of the settings, so a problem can be described with the input that
  led to it. `black.py run --black-box` records a whole run and
  `--replay-input` plays a saved file back to the game.
- **HD Reticle.** General → HD Reticle (Original by default, live) rebuilds the
  game's own HUD atlas at four times its size: a sharper aiming dot and crisper
  small HUD marks, drawn from the game's texels, with no picture shipped. The
  game still colours the dot (white, green on an ally, red on an enemy, black
  on a destructible object). It works with or without the HD texture pack.
- **Sniper in the First Mission.** A Mods row (off by default) puts the sniper
  rifle in your second slot when the first mission starts, with five
  magazines; the mission's own first weapon stays. Continuing from a
  checkpoint keeps the checkpoint's weapons.
- **Lower CPU use.** The graphics acknowledgement thread sleeps 200 µs instead
  of yielding in a loop: in a mission the app's CPU fell from about 116 % to
  30-56 % at the same 30 fps.
- **Settings menu.** A long path at the top of a sub-page folds its first steps
  instead of running under the device status; on Controller Diagnostics the
  values no longer overlap the stick monitor.

## 0.8.0 — 2026-11-10

- **A slot's record in Saves.** After each profile save BLACK keeps, beside the
  saves, the mission last played and its difficulty, the time spent in missions
  and the date, and a picture of the game from that run. A slot's page lists
  them under its rows and shows the picture in the rail band beside them; the
  Saves list shows the selected slot's picture. The profile is not changed: the
  record names the slot's bytes, so after an import or a restore the page shows
  the profile alone.
- **Mission statistics and best results.** Each mission run counts its time
  played, deaths (Mission Failed's Continue and Restart), the checkpoints the
  game reached, shots and the shots that hit a soldier. A completed run joins
  the slot's record: Saves → slot → Missions shows the best time, fewest deaths,
  best accuracy, completions and the last run of the selected mission.
- **Start Mission stays Start Mission.** While checkpoints are on, the title's
  own button reads Start Mission with or without a checkpoint; after a mission
  retired the checkpoint it went back to the game's own Continue Mission label,
  which opens the mission list.

## 0.7.1 — 2026-11-09

- **Settings prompts as on the game's pages.** Select, Confirm and Back stand in a
  column under the description, one row apart, the buttons centred before the
  words; Select shows a cross where left and right change the value, and Default
  stands beside Confirm in the values' column. The heading and the list sit
  nearer the top; seven rows show and the rest scroll, so a row's space parts
  the list from the description, whose two lines no longer touch.
- **A slot's checkpoint** in Saves names the mission in campaign order and the
  checkpoint reached (it showed the game's level number and always part 1).
- **New screenshots** of the settings and of checkpoints in the README.
## 0.7.0 — 2026-11-09

- **Checkpoints kept between launches.** The game's own checkpoint is written
  beside the saves when it is reached. **Continue Mission** on the title resumes
  it after quitting or a restart of the app; the game's own button, which opens
  the mission list, reads **Start Mission** meanwhile and starts the chosen
  mission from its start. The game's own Restart in the pause and Mission Failed
  menus starts the mission over and removes its checkpoint, as the console does.
  Completing a mission retires the checkpoints of that profile.
- **Restart from the checkpoint:** F9 or Back + Y in a mission ask first, in the
  pause page's style, then go back to the run's last checkpoint.
- **Checkpoints follow Autosave.** With the game's Autosave off they are neither
  written nor restored. Options → Profile gains a **Checkpoints On / Off** row
  under Autosave, the same setting as **Saves → Save Checkpoints**; each slot's
  page shows its checkpoint and can delete it.
- **Display Brightness and Display Gamma** on General, over the game's own
  brightness: black level and midtones, live; 0 and 1.00 keep the game's picture
  exactly.
## 0.6.1 — 2026-11-09

- **Saved profiles.** After the first profile list Load Profile said there were
  no profiles and Save Profile showed every slot as damaged, although the file
  was intact: the runtime went on with an old directory scan when the game
  reused a handle. Closing a handle now ends its scan, and saving can reserve
  file space as on the Xbox.
- **Saves** in the settings: the three profile slots with their progress,
  missions, import from a zip, a save folder or a profile file, export, delete
  and clear. A deleted slot is written as a console writes an empty one, the
  slot the game autosaves to is greyed out, every change asks first and keeps
  a backup; backups can be restored or moved to the Trash.
- **The settings as the game's own menu.** General, Controls, Mods and Saves
  open their pages, Back climbs one level, the selected item is described under
  the list. The rail band, its light and the menu's place and type size follow
  the game's main menu. Y / Triangle or Backspace resets a setting, a page's
  last row resets the page, and Reset All Settings is on General.
- **Name keyboard.** X deletes (held, it repeats), Y types a space and Start
  presses DONE, with matching button hints.
- **Menu repeat.** A held direction in the game's menus steps once, then every
  150 ms after 400 ms, instead of racing past items at 60 fps.
- **The Mac window.** Size, place and full screen are remembered between
  launches; a window on a monitor that is gone comes back centred. Hide BLACK
  (Cmd+H), Hide Others, and a Window menu with Minimize (Cmd+M), Zoom and Enter
  Full Screen (Ctrl+Cmd+F).

## 0.6.0 — 2026-11-08

- **The game's own brightness.** BLACK builds a gamma ramp for the Xbox's video
  output: its brightness curve, the brightness slider and its fades to and from
  black. That ramp never reached the screen; the presenter now applies it.
- **Home / Xbox Guide / PS.** A tap opens or closes the settings. Held for 1.5
  seconds it opens a quit page in the style of the game's own menus: Continue,
  Quit to Main Menu (in a mission, through the game's own confirmation) or Quit
  Game. While BLACK has focus the button no longer opens Game Center.
- **Quit Game** in the settings footer and on Port Settings.
- **Skip Films with a Button** (Mods, on by default): A, Start or Enter skips any
  film, also the logos, with a hint after 2.5 seconds. The automatic options are
  now Auto-Skip Intro Films and Auto-Skip Mission Briefings.
- **Effect buffers at higher render scales.** The game's glow and blur buffers
  are averaged over one Xbox texel, so reloads on bright backgrounds no longer
  flicker as a grid.
- **Dock.** A click on the Dock icon brings BLACK back; Quit in the Dock menu,
  log out and restart close it normally.
- **PRESS START** keeps the game's own words instead of a button icon.

## 0.5.5 — 2026-11-07

- **Frame rate in later areas.** After the level streamed in its next part the
  game could fall to 15-18 fps and stay there. BLACK reads its depth buffer as a
  colour texture and reuses colour targets as depth for its light passes; the
  emulated GPU took every such alias through guest RAM at the render scale. Both
  directions are now converted on the GPU, and the first mission holds 30 fps.
- **Bloom and sun shafts.** The same round trip lost the bright glare in
  openings and the sun shafts. They now render where the Xbox shows them.

## 0.5.4 — 2026-11-06

- **Title screen text.** Since the first macOS build the title screen showed only
  the bracket lines where the Xbox shows PRESS START, and the copyright line and
  the logo were missing. They are drawn inside a depth-only mask that an old
  "menu black panel" workaround in the pinned host collapsed to a point. With
  xemu's renderer the mask only writes depth, so BLACK now turns the workaround
  off.

## 0.5.3 — 2026-11-05

- **HD textures: grenade crate and pickup weapons.** PCSX2 pack files keep PS2
  alpha, which for some textures means something else than the Xbox channel.
  Scaling it painted the first mission's grenade crate black with white grenades
  and weapons lying on the ground pale white. Where the game's own alpha varies,
  BLACK now compares the two masks and keeps the game's texture when they
  disagree. The rifle in hand, ammunition and most effects keep their HD files;
  in the first mission five textures fall back.

## 0.5.2 — 2026-11-05

- **Controller hotplug.** Controllers are matched by their SDL identity every
  250 ms, not only when the device count changes, so a pad that macOS replaces
  at the same count is picked up. A detached pad is closed on the poll that sees
  it; held sticks, triggers and aim return to neutral, and keyboard/mouse take
  over under Auto. Rumble is stopped before close and after open, and an effect
  still active from the old connection does not resume. Disconnecting cancels
  button capture and stick calibration without changing saved values; a new
  pad's held buttons do not act in the menu.
- **Bluetooth PlayStation controllers after the desktop.** With vibration off
  the pad stayed in its change-only report mode and its Bluetooth link idled:
  after about five minutes away, the first input arrived four seconds late. The
  pad now streams full reports (its accelerometer is enabled, not read), and the
  delay measured about two seconds after a fullscreen return.
- **HD texture pack check.** Turning on Texture Replacement checks for the pack
  first. Without one it offers to choose a folder; folders without DDS/PNG files
  or without any file the map names are rejected with the reason, an incomplete
  pack is reported with its coverage, and a valid folder is linked into
  `Textures/replacement` without copying and used without restarting.
- Russian launcher runs prepare the Russian strings and fonts without Finder.

## 0.5.1 — 2026-11-04

- Forward the game's native XInputSetState feedback to the host controller,
  respecting input ownership, vibration enable and intensity. Version 0.5.0
  acknowledged feedback without sending it to the motors.
  Keep BLACK's own vibration flags in sync with Controls after profile loads;
  changes in the original Options/Pause menus also update the port setting.
  SDL rumble failures now appear in diagnostics. Bluetooth DualSense vibration
  and disabling it through Controls have been confirmed physically; USB and
  hotplug remain unverified.
- Silence game audio while the recomp menu pauses gameplay, with a short
  fade and paced silent buffers. Closing clears queued silence and fades back
  in, preventing repeated speech fragments while its stream producer is paused.
- Restore the game/menu first responder and relative mouse capture on focus
  return. Clear old menu input suppression after foreground/device changes, so
  an inactive controller cannot keep blocking the selected device's buttons.
  The user confirmed speech/menu behaviour, immediate input after desktop return
  and mouse fire after switching from the gamepad on the development Mac.

## 0.5.0 — 2026-11-03

- Unified **Controls** home puts device and prompt choice first. One binding
  editor selects keyboard/mouse or gamepad independently of gameplay input.
  Mouse and gamepad response have their own pages with X/Y pairs; diagnostics
  and driver settings move under Advanced. Back restores the previous row,
  and named resets affect only the current page or edited binding list.

- **Skip Intro Movies** keeps working after menu weapon previews. Mag/SMG/MP5
  films had incorrectly closed the startup phase, leaving the later developer
  credits (`C_N_US`) and opening film unskipped. Only campaign films now close it.

- Saved **Input Device** selection (Auto, Gamepad, Keyboard) in Controls.
  An explicit launcher override lasts for that run; editing the option clears it.
- **Controller Bindings** for 16 Xbox buttons/triggers, with button capture,
  cancellation, unbinding and reset. Menus retain their physical controls.
- **Button Prompts**: Auto, Xbox, PlayStation or Keyboard, in the native settings
  and guest menus/hints. Keyboard hints follow the saved binding. PlayStation
  button pictures replace plain font symbols: coloured face buttons, shoulders,
  triggers, sticks, keyboard caps and mouse buttons, shared with native settings.
  Their host atlas preserves the guest file heap and source game files.
- Restore relative mouse look when switching from pad to keyboard during
  gameplay or inside settings. Pad selection, Escape and focus loss release it.
- README settings captures updated to the current green-grey menu style.

## 0.4.1 — 2026-11-03

- **Russian film subtitles.** With Russian game text, the mission briefing films
  show Russian subtitles: the nine tracks ship with the app and turn on by
  themselves.
- **Wider Cyrillic in the setup.** Text the game's fonts cannot draw, such as
  РУССКИЙ, uses the settings panel's wide faces instead of a narrow stand-in.

## 0.4.0 — 2026-11-03

- **First-time setup** in the look of BLACK's own menu pages: rails, the heading
  typed in, green-grey rows and the selector bar, the game's fonts read from your
  disc. Choose or drop an ISO/XISO or folder, review the release, languages, size
  and free space, then copy with progress, speed and time left. **Stop** keeps
  the copied files and the next install carries on from them. Then choose the
  game text from your disc's languages (the Mac's own language first when the
  disc has it), the settings panel's language, and render scale, anti-aliasing,
  upscaling, texture filtering and field of view, or the recommended set.
- **Russian game text.** The app carries the translation text only. Choosing
  Russian in setup or settings builds the Russian string bank from your disc's
  English bank and adds Cyrillic to your own copy of the game's fonts (Latin
  text in every other language is unchanged), automatically before each start.
- **Recomp settings** now use the game's menu palette and rails instead of the
  gold palette carried over from TGP's other ports. Layout and controls are unchanged.
- German, Spanish and Italian game text checked from the menus into the first
  mission. `tools/macos/check_languages.py` checks that every installed string
  bank has the English IDs and that the game fonts can draw its letters;
  `black-setup-preview` draws the setup pages and stops and resumes a real copy.

## 0.3.0 — 2026-11-02

- **Game Language** in Recomp Settings → General: English, French, German,
  Spanish or Italian game text from your disc's own string banks, applied on
  the next launch. The European disc has all five; the USA disc has English.
- **Menu Language** for the settings panel: Auto (follows the game), English,
  French, German, Spanish, Italian or Russian. Labels are translated in every
  language, help lines in Russian; the game itself has no Russian text.
- `black.py build` and `run` now use the ANGLE Metal renderer by default;
  `--opengl` keeps the slower legacy OpenGL path for comparisons. A run started
  without `--metal` had silently used OpenGL (17 FPS where Metal held 30).
- Quitting no longer clicks in the speakers: game audio fades out over 20 ms
  and the device closes before the app exits.
- **Texture Replacement** in Recomp Settings → Textures, off by default: HD
  textures from your own copy of the PCSX2 "BLACK HD Reimagined" pack, placed
  as it is under `~/Library/Application Support/BLACK/Textures/replacement/`.
  portingpete's MIT-licensed map, shipped with the app, names the pack file for
  each Xbox texture. Files load in the background, once each, and stay on the
  GPU; a missing or unreadable file keeps the game's own texture. The pack's
  PS2-scale alpha follows the game's own texture, so opaque surfaces stay
  opaque and foliage keeps its cut-outs. **HD Texture Size** (1024, 2048 or
  4096, default 2048) caps the largest side to save memory. The pack is not
  included.
- A malformed texture mode no longer aborts the game: a PROJECT2D texture that
  reports no dimension is sampled as 2D, as the sampler already chose. A long
  fourth-mission session had ended with SIGABRT on that shader.
- Four more HD texture matches (the Boxes01 atlas, the Recon_Secret screen, a
  first-aid label and door 1212) in `black-hd-reimagined-extra.map`, shipped
  beside portingpete's map, whose entries take precedence.
- Developer options for `black.py run`: `--texture-audit` (unique texture keys
  and map matches, no pixels), `--texture-dump` (original top levels into the
  run folder), `--subtitle-dir` (experimental SRT captions for cutscenes) and
  `--test-game-view` (a separate validated game folder for isolated checks).
- Mods page: **Infinite Ammo** for any weapon you hold, **Rapid Fire** (2-4X),
  **No Recoil**, **One-Hit Kill** and **Game Speed** (50-200%), from
  TGP482/BLACK-XBERecomp PR 141. God Mode now also holds the game's own
  invulnerability flag, so a single huge hit no longer kills. The old
  "Infinite Ammo (Silver Weapons)" is now **Silver Weapons**, and the "BFG
  (M249) Code" is **M249 in the First Mission**, which is all it does.
- `tools/macos/russian_font.py` rebuilds the game font of a Russian fan
  translation you own: real digits 3, 4 and 6, the missing Й Э Щ Ъ Ё, and a З
  that no longer reads as Э. No translation or font is included.

## 0.2.0 — 2026-11-01

- Settings gain a fourth **Mods** page, laid out like TGP's menu: skip intro
  and mission films, God Mode, infinite medkits and grenades, infinite ammo
  through the game's Silver Weapons, Always Silenced, Unlock All Missions,
  Unlock Black Ops Difficulty, M16A2 at mission start and the BFG code.
  Addresses are kept per XBE revision; an unknown executable leaves mods off.
  Everything is off by default.
- **FPS Counter** in Recomp Settings → Display shows the game's frame rate in
  a small corner label.
- Texture replacement is not available on macOS yet. TGP's packs are keyed by
  Xbox texture bytes, so the PCSX2 HD Reimagined pack for the PS2 version does
  not apply directly.

## 0.1.0 — 2026-11-01

First experimental Apple Silicon release of the Original Xbox BLACK port.

- Native ARM64 host, ANGLE Metal renderer and DSP audio path.
- macOS app identity, icon, native fullscreen and Game Mode opt-in.
- Styled drag-to-Applications DMG and installation of your own supported
  USA/European disc image or extracted game folder.
- TGP-inspired settings, controller calibration/sensitivity, keyboard bindings,
  direct mouse look and Flash menu cursor/name entry.
- Profile save/load evidence and fixes for audio completion, interrupt handoff
  and NV2A FIFO/fence publication races.
- Separate Library data and unique session logs, without bundled game assets.

Tested on Apple M1 Pro / macOS 27.0.1; the download requires macOS 27+.
A complete playthrough, guaranteed performance and other platforms are not qualified.
See [validation](docs/VALIDATION.md).
