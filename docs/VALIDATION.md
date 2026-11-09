# Release validation and current limits

Release: **BLACK 0.7.0**, Apple Silicon. Test machine: Apple M1 Pro,
macOS 27.0.1. This is an experimental release, not a complete-game qualification.

## Checkpoints and display brightness in 0.7.0

**Checkpoints.** The game keeps one checkpoint in memory (live flag `0x2F3BF8`)
and loses it on quit. Its capture (`0x1C8D20`) is wrapped and the record is
written atomically to `Data/xboxrecomp/Checkpoints/` per save stamp, player
name, difficulty and mission: the player record, weapons, objective counters and
states, statistics and the streaming unit, no guest pointers (after
portingpete's `black_checkpoint.inc`, MIT). It is restored before the first
streaming unit starts (`0x1EE310`), only for a load that asked for it: the
title's Continue Mission item or F9 / Back + Y. The game's own RestartLevel
(pause, `InGameOn.bin` RestartConfirm) and RestartMission (Mission Failed) clear
the live flag and set the first unit, a restart from the start; they now also
remove the file unless F9 armed the restart. The title stamps a save's date only
into the slot copy it writes (`0x1FB3F0` → `0x1F1A00`), so after a profile save
the running game keeps the old date: the files move to the saved date and this
launch maps one to the other. A changed campaign record on save retires the
slot's checkpoints.

**Autosave.** Checkpoints are written and restored only with the game's Autosave
on (`main+0x20164`, `FE_MemcardAutosaveOn`). Options → Profile gains a
Checkpoints row: copies of the Autosave label and its On / Off values one row
lower, their tokens set again so the fields draw, the label a token of ours
answered by the string lookup in the game's language. It is a focus stop only
while Autosave is on.

**Checked.** On a validation copy of the saves, scripted runs with muted audio
and only scripted input: Continue Mission restored unit 4 of mission 2; Back + Y
in that mission opened the question, Restart from Checkpoint ran RestartLevel and
the reload restored the same checkpoint; the Profile row toggled On / Off, left
the ring with Autosave off (after the game's own confirmation) and returned with
it on, in English and Russian. Unit tests cover capture, the armed restore, both
restarts, a damaged file, F9 without a valid file, a profile save in the same
launch and the Autosave gate. Earlier the user resumed from the title item by
hand.

**Not checked.** A checkpoint reached in normal play on each mission and
difficulty, death and the game's own Continue after a restore, the game's own
Restart removing the file, a profile save in the middle of a session, F9 on the
keyboard and Back + Y on a physical controller.

**Display Brightness and Gamma.** A curve after the game's ramp: brightness
moves the black level, gamma bends the midtones; the presenter composes it into
a 16-bit lookup. At 0 / 1.00 the lookup is the game's table exactly (tested
bitwise). Checked on the main menu frames; not judged in missions.

**Tests.** 121 CTest checks and 49 Python tests passed. The final DMG was
checked read-only in English and Russian with isolated data and system-only
PATH: both opened the settings and quit normally, and source game files and user
preferences kept their hashes. The matching source archive was verified without
game or private data.

## Saves, the settings menu and the Mac window in 0.6.1

**Profile list.** The runtime kept a directory scan by handle value until it
reached the end. The game's profile list stops at the first save folder and the
next open reuses the handle value, so every later list resumed the old scan, read
a file outside the profile folder and marked all three slots damaged. The guest
`NtClose` bridge now ends the scan of the handle it closes, and
`FileAllocationInformation` reserves space as on Windows. On a copy of the
user's saves, repeated Load Profile and Save Profile kept the profile and showed
no damaged slots.

**Saves.** A profile is one signed 0x1800-byte file of three 0x778-byte slots.
An empty slot keeps its marker and sets the empty flag, as console saves from
the user show; zeroing the marker made the game report it as damaged. Edits are
applied on the game thread, only on main-menu pages and only while the game's
in-memory image equals the file, with an atomic write and a backup. The user
deleted test slots and imported a profile, and the game's own Load Profile list
then showed the slots as the page did (empty, not damaged). Export, restoring a
backup and moving backups to the Trash were exercised in development runs only. Unit tests cover
checksums, signing, slot clearing, copying and backup naming.

**Name keyboard and repeat.** On a DualSense the user deleted with Square, typed
a space with Triangle and finished with Start, and checked the d-pad step of
400/150 ms in the game's menus.

**Settings menu.** The nested menu, its resets, greyed rows for the autosave
slot and the composition beside the rail band were reviewed by the user on
screenshots from bounded windowed runs with scripted input.

**Window.** The user resized, moved, entered and left full screen and relaunched;
the window came back as left. Cmd+H, Cmd+M and the Window menu worked. A unit
test covers fitting a saved frame to changed screens. Not checked: a monitor
removed between launches on real hardware, Stage Manager, Spaces assignment.

**Tests.** 119 CTest checks and 49 Python tests passed. The final DMG was checked read-only in
English and Russian with isolated data and system-only PATH: both opened the
settings and quit normally, and source game files and user preferences kept
their hashes. The matching source archive was verified without game or private
data.

## Brightness, Home button and film skipping in 0.6.0

**Gamma ramp.** BLACK builds its ramp at `0x1762B0`:
`255 · 2.2 · (i/255)^1.2 · tint · (1 + brightness) · fade`, clamped to 255, and
queues it for the vertical blank, which loads the DAC only while a flip waits
for it. On this runtime that stops after four flips and the DAC registers are
plain RAM, so neither the curve nor the slider nor the fades reached the screen.
`cmake/BlackGameOverlay.cmake` hands each queued ramp to
`ports/macos/overrides/display_gamma.c`; the presenter applies it last, with
linear interpolation between entries (`run --gamma off` for A/B). Checked: 77
tables in a 60-second run (a fade-in in about 30 steps, then r64=106, r128=245);
frames of the title and logo films with and without the ramp; the slider at
both ends (r64=149, r128=255 at its top); fades on loading and death. Death
turns the screen red in the game's own frame, not in the ramp. The user judged
the picture in missions 1 and 8 as very close to the Xbox.

**Home / Xbox Guide / PS.** A tap toggles the settings, a 1.5-second hold opens
the quit page (`menu_input.h`, tested). In a mission Quit to Main Menu sends
START unless the pause menu is up, then calls that page's own `GoToQuitConfirm`
(`InGameOn.bin`), which opens the game's ConfirmWarning with `QuitLevel` on YES.
A scripted run (`--test-home-quit`) reached PauseMenu and ConfirmWarning, also
in the release configuration with the real DSP. While the window has focus each
controller's Home is set to `GCSystemGestureStateDisabled`; macOS honours that
only for an app declaring `GCSupportsControllerUserInteraction`, now in
`BLACK-Info.plist` (a bare executable started by `black.py run` has no
Info.plist; use `--game-mode`). The user confirmed Game Center no longer opens.

**Dock.** The host pumps events itself and never ran
`-[NSApplication finishLaunching]`, so reopen and quit Apple events went
unanswered. It now runs it and handles both: a quit event closed a test run
with exit code 0, and with the release app the user brought the game back with
a Dock click and closed it from the Dock menu. A minimized window and log out
or restart with the game open were not tried.

**Films.** Skip Films with a Button: a press the game does not answer within
10 polls while the same non-looping film plays requests the game's stop byte
(`player+0x1B9`). Scripted: the logos, the intro and the next film skipped,
the looping menu film kept; with the option off nothing is skipped. The hint
appears after 2.5 seconds (tested). PRESS START keeps the game's text.

**Effect buffers.** At scales above 1× the 64x32, 80x60, 160x120 and 320x240
glow/blur targets read with linear filtering are averaged over one Xbox texel
when copied into a texture (`xemu_effect_filter.inc`; the 160x120 depth-as-colour
target, read with box minification, is untouched; `--effect-filter off` for A/B).
The user no longer saw the grid on reloads against a bright background.

One unexplained frozen picture occurred once on a second mission start with
frame capture on; it did not recur without capture or with the new features
off. **115 CTest checks** and **49 Python tests** passed. The final DMG was
checked read-only in English and Russian with isolated data and system-only
PATH; the source archive matched the local sources and pinned upstreams.

## Frame rate, bloom and sun shafts in 0.5.5

In the first mission the user saw the game fall from 30 to 15-18 fps after the
level streamed in its next part, and stay there. Profiles of the live game
showed the emulated GPU thread busy on `glReadPixels` and `glTexImage2D` while
the game waited for it. BLACK reuses render-target memory under another format:
it reads the 640x480 Z24S8 depth buffer through an A8R8G8B8 texture header, and
it writes downsampled depth as colour into a 160x120 target that its light-cone
passes then bind as their depth buffer. Stock xemu has no GPU path for either
alias ("FIXME: Support zeta to color"), so each one went through guest RAM at
the render scale every frame, and the result of that round trip also lost the
bloom and sun shafts. `ports/macos/overrides/xemu_surface_alias.inc`, applied
to the build-local copy of xemu's `surface.c`, converts both directions with
shaders in the Xbox byte layout (A = Z[23:16], R = Z[15:8], G = Z[7:0]) for
same-sized, unswizzled A8R8G8B8 and fixed-point Z24S8 targets whose GPU copy is
current; everything else keeps the stock path.

The conversion runs from the first seconds of the first mission (the 160x120
target at `0x0365C000` changes format every frame). The bright glare in the
opening gate matches a frame of an Xbox playthrough video. The user then played
the whole first mission with the fix: no slowdown, bloom and sun shafts where
they belong, and no artefacts seen. Of 1190 two-second FPS samples in that run
one was below 25 (3 fps while a part loaded); before the fix the same area ran
at 15-18 fps. **114 CTest checks** and **49 Python tests** passed.

## Title screen text in 0.5.4

Since the first macOS build the title screen showed only the two bracket lines
where the Xbox shows PRESS START; the copyright line and the title logo were
missing too. Those fields draw with a `LEQUAL` depth test inside a depth-only
mask the menu draws first: a black triangle rectangle with colour writes off.
The pinned host's "menu black panel" workaround (`black_panel_filter`) collapsed
every such rectangle to a point before PGRAPH, a fix for an older renderer that
painted the mask black. With xemu's PGRAPH the mask only writes depth, so the
collapse hid the text. `ports/macos/overrides/main_posix.c` now turns that
filter off (`RECOMP_KEEP_BLACK_PANEL=1`) without changing the submodule. Checked
in English and Russian with the HD pack: PRESS START, the copyright and the
logo are back, and the main menu and mission select show no black panel. The
user confirmed the screen live. Later scripted captures of the options
menu, the operator profile and the pause menu in the first mission also show no
black panel.

## HD texture alpha in 0.5.3

The user reported a black grenade crate and a pale rifle in the first mission
with HD textures on. Runtime dumps identified the crate as `bp1_hgr_amo`: the
Xbox alpha shades the whole crate, while the pack file's is a cut-out of the
grenades (0 elsewhere), so scaling left the crate black and the grenades
white. A rifle lying on the ground (`bg1_ak1_lod`) has a reflection mask on
Xbox and alpha flat at 128 in the pack, which scaled to full reflection;
that was the rifle the user reported. The M16 held in the mission keeps its
correct colour. Comparing 25 first-mission pairs whose game alpha varies set the
rule: on an 8x8 grid, a mean difference over 48 with correlation under 0.8
keeps the game's texture. In the user's live session five textures fell back,
including both reported ones; the user confirmed the crate and the rifle on the
ground now look right. The other 108 pack files loaded without errors.

## Controller hotplug, Bluetooth idle and HD pack check in 0.5.2

The user checked these on the development Mac with **DualSense controllers**
(macOS 27.0.1, SDL2-compat 2.32.72, raw HID backend); each session quit
normally before its bound:

- **USB:** six connects and five disconnects in one session; held input went
  neutral, keyboard took over under Auto and the PlayStation prompts returned.
  With R2 held, fire and motors stopped on disconnect and the first shot after
  reconnecting vibrated.
- **Bluetooth:** three deliberate disconnect/reconnect cycles with R2 and a full
  left stick held; capture of a rebind was cancelled with the saved binding
  kept; vibration stopped and returned on the first shot after reconnecting.
  Disconnecting during stick calibration reported the loss and changed nothing.
- USB→Bluetooth and Bluetooth→USB in one session; manual Gamepad and Keyboard
  input choices; aiming is cancelled when the pad disconnects.
- **Two DualSense controllers** (USB and Bluetooth) in both orders: port 0 plays,
  the second does not interfere, and its disconnect/reconnect leaves port 0's
  input and vibration intact. Choosing which pad plays is not implemented.
- **Desktop return, Bluetooth:** windowed, input answered in about a second after
  up to four minutes. Fullscreen after about five minutes the first input came
  **4 s** late, with and without Game Mode, while SDL kept the same device: the
  idle pad's change-only reports let the link sleep. With full reports it came
  after about **2 s** (the user felt the two seconds); report pauses at the
  fullscreen switch remain unexplained. A complete loss of input was reported
  once on 0.5.1 and not reproduced.
- **HD texture pack:** a live Russian run showed the missing-pack dialog,
  rejected a folder without textures and a folder of unrelated pictures, linked
  the real pack into an isolated texture folder and used it without restarting;
  turning it off and on again needed no dialog.

The source passes **114 CTest checks** and GPU probes, including a synthetic
hotplug test of identity reconciliation, retry, three replacement cycles,
rumble stop/rearm, capture/calibration cancel, two pads and full reports, and
texture pack checks (empty, complete, incomplete and unrelated folders, link
replacement). All **49 Python tests** passed. The final 0.5.2 DMG was mounted
read-only and launched in English and Russian with isolated data, system-only
PATH and no DYLD_LIBRARY_PATH; both runs opened settings and quit normally, and
source game files and user preferences kept their hashes. Other controller
models remain open.

## Fixes and physical checks in 0.5.1

The user confirmed these fixes on the development Mac with a **Bluetooth
DualSense and keyboard/mouse**:

- Camera movement and mouse fire after switching from the gamepad.
- One physical Reload-to-Square button rebind, restored after the check.
- Controller vibration and disabling it through the settings menu.
- Speech no longer looping while the recomp menu is open, with normal audio
  after closing it.
- Immediate input after returning from the macOS desktop.

The final physical candidate exited normally with code 0 before its bound.
This qualifies those scenarios on that hardware, not every input effect,
controller or transport. USB and Bluetooth hotplug are covered in 0.5.2 above.

The source passes **113 CTest checks** and GPU readback/colour probes. New
production-source regressions cover the native Xbox feedback layout/completion,
menu audio fade/queue pacing/resume, focus capture and stale menu suppression.
A separate bounded scripted run submitted 724480 silent PCM frames while the
pre-mute game mixer remained nonzero; it quit normally before its limit. Dummy
audio validates the output gate, not sound quality; the user's listening check
is separate. All **43 Python packaging/disc tests** passed.

The final 0.5.1 DMG was mounted read-only. Its bundled app launched in English
and Russian with system-only PATH and no DYLD_LIBRARY_PATH, using isolated data.
Both runs opened settings, prepared the host font atlas and quit normally with
code 0 before their limits; neither required a forced kill. Private font views
were removed, and source XBE/fonts/bank and user preferences retained their
hashes. The arm64 library closure, macOS 27 minimum, app signature and
Applications link were checked. Matching source archives/checksums contain no
game or private data. These packaging checks do not qualify a clean second Mac
or a complete playthrough.

## Input and menu changes in 0.5.0

The input UI now has one **Controls** home with five rows. Device and prompt
selection are visible without scrolling; bindings, mouse response and gamepad
response open separate pages. The native Cocoa preview exercised independent
X/Y values, editor selection without a device change, page-scoped resets,
binding capture/cancel/disconnect, stable focus across input changes, return
navigation and quit. English/Russian views were inspected at 960×600 and
640×480. A bounded muted/script-only app run (`run-1uqe0py6`) verified the
actual menu, produced the updated README captures and exited through Cmd+Q
with code 0 before its limit, leaving no child. Source fonts, bank and XBE
hashes were preserved. This does not qualify physical hardware or gameplay.

Version 0.5.0 adds saved input-device selection, controller button bindings
and Auto/Xbox/PlayStation/Keyboard prompts. The README settings captures come
from the running build.
The 0.5.0 source passed **110 CTest checks**, GPU probes and **43 Python tests**.
The later physical checks and fixes are described above. [Input evidence](INPUT.md).

The intro-skip policy now also excludes menu weapon previews from the transition
that ends startup. A new regression fails on the previous policy and passes on
the fix: Mag/SMG/MP5 cannot disable the later C_/02_ skip; campaign films still
end startup. The current user's running session was retained at their request,
so the exact credits-after-preview sequence still needs a new visual check.

## Evidence

- The user confirmed combat and game menus after the NV2A FIFO handoff fix.
  A 600-second session ended at its time limit without a forced kill or observed
  crash/hang. That timeout is not a new gameplay or quit PASS.
- The user has repeatedly confirmed Cmd+Q. Separate automated local quit checks
  also exited with code 0 before their limits and left no child process.
- The user's profile was saved and the next process read the full 6,144-byte
  Profile successfully. Restoring every checkpoint/completed-mission result has
  not been independently qualified.
- The initial packaging baseline passed **100 CTest checks**, framebuffer/D3DCOLOR GPU probes and **30 Python checks**. These include native-C hook math, register preservation, race negative
  controls, settings persistence and the disc importer. They are not a substitute
  for a playthrough or physical controller testing.
- The Flash cursor opened Main → Options → Sound Setup; the wheel selected
  Music Volume, Escape returned, and profile pages opened. Keyboard entry showed
  `BLACK`; Backspace removed the last character. The test name was cancelled
  without saving a new profile.
- Game Mode was observed on in native fullscreen and off after exit, with the
  system policy unchanged at automatic. No FPS/latency benefit was measured.
- A relocated app ran with only system directories on PATH and without
  DYLD_LIBRARY_PATH. Its loaded non-system libraries came from the app bundle.
  It created a unique session log and exited normally before the external limit.
- First-run game import and a subsequent app session completed in an isolated
  test data root. The original development settings and all UserData hashes
  were preserved. The source game was not modified.
- The DMG's app signature, Applications link, Retina background and saved Finder
  icon/window layout were checked. The installed `/Applications/BLACK.app` also
  exited with code 0 before its limit, using system-only PATH and bundled dylibs.
  GUI screenshot inspection of the final Finder window was unavailable in this
  environment; this is not described as a clean-machine hardware qualification.

## Mods page and FPS counter (0.2.0)

New in 0.2.0. The settings panel gains a fourth **Mods**
page laid out like TGP's menu, and Recomp Settings → Display gains
**FPS Counter**, a small corner label counting the game's finished frames.

| Group | Mods | Takes effect |
| --- | --- | --- |
| Films | Skip Intro Movies, Skip Mission Intros (moved from Recomp Settings) | Immediately |
| Player | God Mode (health refilled every frame), Infinite Medkits (3), Infinite Grenades (9), Always Silenced (experimental) | Immediately |
| Player | Infinite Ammo through the game's own Silver Weapons reward | Next mission start or restart |
| Unlocks | All missions; Black Ops difficulty; BFG code | Saved into the profile; turning a mod off never relocks |
| Unlocks | M16A2 at mission start | Next mission start or restart |

Addresses are kept per XBE revision and chosen by the executable's header
timestamp. The Mac build runs code lifted from the USA executable, and the
supported European disc is normalized to it, so both discs report the USA
revision. An unknown executable shows `MODS OFF` and nothing is written.

- New unit tests cover revisions, both health scales, menus/loading without
  writes, unlocks, campaign-only spawn rewards and the silencer point; the menu
  preview covers all four pages. **101 CTest checks** and **30 Python checks**
  passed.
- First mission on Normal with the player mods on: the log reported the USA
  revision, `silver=1 m16a2=1` at spawn and `health=1200.0 max=1200.0
  medkits=3 grenades=9` every 150 frames. A window capture showed three medkits,
  nine grenades, 999 rounds, the M16A2 and `FPS: 29.9`.
- The packaged 0.2.0 app, run from its mounted DMG with a system-only PATH,
  enabled the DSP56300 JIT, reported the USA revision and quit normally with
  code 0 before its limit.
- Scripted pad only. Damage above the maximum in one frame can still kill.
  Always Silenced and the BFG were not exercised in combat. The trainer's weapon
  swap and faster game speed were not ported.

## Mods from TGP482/BLACK-XBERecomp PR 141 (0.3.0)

PR 141 (ported there from Black-Android) adds cheats as TGP C++ hooks. The same
USA addresses are used here through the native-C overlay: the bullet hit
0x1E74B0 and the weapon update 0x1EEFD0 are renamed and wrapped, and TGP's
RecoilDecay point 0x1E5FB7 is a verified anchor. ESP, the aimbot and the weapon
spawner were not ported (ImGui drawing, other levels' assets).

| Mod | Evidence (first mission, Normal, Metal, `--state-log`; runs under the worktree's `out/macos/user`) |
| --- | --- |
| Infinite Ammo | Fire held: off, the magazine went 15 to 0 and reloaded; on, 90 shots with the magazine at 15 throughout (`run-7czmr2hl`) |
| Rapid Fire 4X | A pistol shot every 10 frames off, every 3 on (one shot a frame is the ceiling at 30 fps, so 3X and 4X match on it; `run-7czmr2hl`) |
| No Recoil | M249 fire held: off, the view kick added up 2.4 to 7.6; on, each step started from 0 (`run-zn2kgcwv`) |
| God Mode | Own grenade at the feet: off, health 1200 to 908.8; on, 1200 throughout with the invulnerability byte set (`run-en78c3gh`) |
| One-Hit Kill | Played by hand: 8 soldiers, each `health 100.0 -> 0.0 dying=1` after one pistol shot (`run-mw8kcox1`) |
| Game Speed | 30 fps held; a shot every 18 / 10 / 5 frames at 50 / 100 / 200%; a 1.2 s walk covered 2.59 / 5.08 units at 50 / 100% (`run-3byu5png`, `run-8o6ovkx8`) |
| M249 in the First Mission | The developers' code (0x2D12AD) gave `BG1_HVY` with 600 rounds in mission 1; the gun has no texture there (`run-jriqm4ya`) |

- The game's scripts set the invulnerability byte (0x8F2 on the actor) during
  the mission intro; God Mode clears only the byte it set itself.
- New unit checks cover God Mode's byte, One-Hit Kill and its refund, ammo,
  Rapid Fire, No Recoil and the value steps; 103 CTest checks passed.
- Not checked: other weapons and missions, Rapid Fire with the under-barrel
  launcher, Game Speed above 100% in heavy scenes.

## First-time setup (0.4.0)

`black-setup-preview` with the extracted European folder, the European ISO and
the USA ISO: each was inspected (Europe 685 files / 2.21 GB with five language
banks; USA 574 files / 1.69 GB, English), stopped at a third of the copy with
the partial files kept and no `Game` folder, then resumed through the setup's own
flow and validated (USA executable hash). Both ISO SHA-256 values were unchanged
afterwards. The keyboard moved the selection; every page was drawn to PNG.

The real binary with an empty data folder and `BLACK_TEST_SETUP_SOURCE` set ran
the setup unattended (check, copy, Play), then started the game, which quit
normally at its test time with exit code 0 and no process left. A person has not
yet used the setup with a mouse or a physical controller, or dropped a file on it.
105 CTest checks passed. The settings panel kept its layout and every preview page;
its colours changed.

Language and graphics pages: the preview changed render scale with the right
arrow (saved as 3), restored the recommended set (2x, SMAA, MetalFX, game
filtering, 70 degrees) and stepped the game text, each written to its own
settings file at once. Unattended with German chosen in the setup, the real
binary installed the European folder and the game opened `MainGe.bin` on its
first start, then quit normally with exit code 0.

## Russian film subtitles (0.4.1)

The real binary with an empty data folder, Russian chosen in setup and films not
skipped: the start turned the subtitles on, the first mission's briefing film
`03_N` loaded its 35 cues and the cues changed with the film (27 selections in the
run). The person watching confirmed they showed correctly. Only that film was
watched; the other eight tracks were checked as SRT (UTF-8, ordered times).

The setup drew Cyrillic it could not take from the game's fonts in narrow DIN
Condensed. Once the disc's fonts are read it now falls back to Arial Black /
Arial Bold, as the settings panel does; `black-setup-preview` on the European ISO
showed РУССКИЙ at the width of the Latin rows.

The packaged 0.4.1 app, run from its mounted DMG with the system PATH and an
empty data folder: setup installed the European ISO with Russian chosen, the
start built the Russian bank and turned the film subtitles on, and the app quit
with exit code 0. Films were not reached in that run.

## Russian game text (0.4.0)

The fonts were found to keep their glyph records sorted by `(code & 127, code)`
with 128 bucket words pointing at each run; records placed outside that order
drew the default box, and appending records without rebuilding the buckets made
every glyph a box. With records sorted in and the buckets rebuilt, a test bank
showing А-Я in every string drew all 33 letters in both fonts next to an
unchanged English string. The full Russian bank then showed the main menu,
difficulty choice, briefing, mission objectives and pause menu in Russian.

Through the real app with an empty data folder: setup installed the European
folder with Russian chosen, the start added Cyrillic to both fonts (retail copies
kept), wrote `MainRu.bin` and made `MainUS.bin` Russian; the game ran in Russian
into the first mission and quit with exit code 0. Switching the setting to
English on the same install restored the English `MainUS.bin` without touching
the fonts again. The USA ISO, whose only bank is `MainUS.bin`, gave the same
result. 106 CTest checks passed, including `macos_translation` on synthetic
fonts and banks. Missions after the first, film subtitles and the long credits
were not checked in Russian.

The packaged 0.4.0 app, run from its mounted DMG with the system PATH: setup
installed the European ISO with English and left both fonts at their retail
hashes; switching the setting to Russian then built the Russian bank and the
Cyrillic letters on the next start, and the first mission's objective showed in
Russian. Both runs quit with exit code 0. Russian is prepared only when chosen.

## Languages (0.3.0)

The USA executable picks its string bank
`Language/Strings/Main%s.bin` from a suffix table indexed by a language word
that its start-up fixes at US. The build overlay replaces that constant (two
verified stores at 0x1F8100) with the saved Game Language, offered only when
the installed game has the bank. The game then loads the bank itself.

- French: the log reported `MainFr.bin (index 4)`, the game opened that file,
  and the difficulty menu showed French text with accents in the game's font.
  Only the front end was checked; other languages and in-mission text were not.
- Menu Language: the menu preview exercised Russian and Spanish pages. Text
  the game's font cannot draw (Cyrillic) uses a system font at the same
  capital height. French, German, Spanish and Italian labels follow TGP's
  translations; their help lines stay English.
- 102 CTest and 30 Python checks passed, including language persistence.
- German, Spanish and Italian (0.4.0):
  `tools/macos/check_languages.py` found the same 1724 string IDs in all five
  European banks and a Big.bin and Small.bin glyph for every character beyond
  English. Three muted, scripted runs with isolated saves went from the menus
  into the first mission and its pause menu; each opened its own bank, loaded
  the level and exited normally before the limit. Menus, briefing, mission
  objectives and the pause menu showed the selected language with readable
  accents and no clipped text on those screens. Other missions and the longest
  strings were not checked. 43 Python checks passed.

## HD texture replacement (0.3.0)

**Recomp Settings → Textures → Texture Replacement**
(off by default, saved as `texture_replacement` in settings.ini, applied live)
replaces game textures with files from your own copy of the PCSX2
"BLACK HD Reimagined" pack. Its files are named by PS2 hashes; portingpete's
map, shipped unchanged under MIT, names the pack file for 1161 Xbox textures.

- The key follows portingpete's renderer exactly: 64-bit FNV-1a with each
  little-endian 64-bit word of the top level, as stored in guest memory, as one
  step (DXT as stored, the I8 palette texture still swizzled, the palette
  itself excluded). Width, height and the NV2A colour format are
  separate lookup fields, not hashed.
- The pinned xemu `gl/texture.c` is patched at configure time through four
  checked anchors; the submodule is unchanged. A texture is first made from the
  game's data. Each pack file is read and decoded once on a loader thread and
  becomes one GL texture, kept and shared; it takes the original's place only
  after an error-free upload. A missing file, a decoding failure, an unsupported
  format or a GL error keeps the original.
- The first build read the file again for every remade texture. In a manual
  session the game remade some mapped textures about 60 times a second (the
  cause was not traced; the key leaves out the palette), so the originals kept
  showing while files loaded and the pack looked missing. Pack textures are now
  shared and kept up to 1 GB when unused, so a remade texture gets its HD
  replacement at once.
- Alpha follows the game's own texture. PCSX2 pictures keep the PS2's alpha,
  on which 128 is opaque, and many of the pack's pictures of opaque surfaces
  carry alpha the Xbox reads (maximum 128 in 323 mapped files, 156–157 in 323
  more). Where the game's texture is fully opaque, the replacement's alpha is
  ignored (texture swizzle); elsewhere it is scaled so its highest value matches
  the game's. Before this, a grenade crate in the first mission rendered black
  and the second mission's pine crowns vanished; with the fix, a scripted
  second-mission capture shows the crowns again, close to the original.
- Pack files are found by bare name anywhere under
  `~/Library/Application Support/BLACK/Textures/replacement/`, also through a
  folder link to another volume. DDS BC1–BC3/BC7 upload compressed; PNG is
  decoded through ImageIO without premultiplication and gets a mip chain.
- A new unit test checks the key against reference values, map parsing, the
  shipped map (1161 entries, 1007 files), DDS/PNG decoding and name lookup in a
  PCSX2-style folder layout. **103 CTest checks**, **30 Python checks** and the
  native menu preview, including the new row in Russian, passed.
- The user's copy of the pack (1324 files, all 1007 mapped files present), in
  bounded, muted, scripted first-mission runs:
  - Off: no texture-pack activity in the log.
  - On: of 191 eligible textures, 111 were replaced and 80 had no map entry;
    no failures; 382 MB of HD textures resident after loading the level. The
    game's own 30.0 FPS was unchanged. The single level-load frame took 211 ms
    (181 ms with the option off).
  - On, with an empty folder: the log reports that replacement stays off and the
    level loads normally.
  - On, after the fix, 170 seconds with longer walking: 1,462 textures were
    replaced from 127 file reads, with no failures, 448 MB resident and the
    game at 30 FPS. The slowest frame (117 ms) came while no file was loading.
  - Manual: turning the option on in the settings panel while the game ran
    loaded the pack and remade the textures without a restart.
  All runs stopped at their time limit without a forced kill. Window captures
  from the same frame show HD concrete, wood and floor art from the pack where
  the original textures were.
- The old progress counters count texture creations, including repeated
  creations of the same content. They are not a percentage of unique textures
  covered by the map. A second-mission audit found **94 unique keys: 50 mapped,
  44 unmapped**, despite the progress log reporting 1,208 creations and 1,158
  misses. Of those 44 unmapped keys, 38 exactly match original top-level bytes
  on the disc but have no map entry; the other six are outside the inspector's
  serialized top-level inventory and remain unidentified. No observed mapped
  key was lost at lookup. Files existing in a PS2 pack do not establish a
  corresponding Xbox map entry.
- A read-only inspection of the disc found 218 unique eligible top levels in
  the second mission's serialized files, of which 112 have map entries. These
  are file inventory counts, not a claim that every texture was drawn. All
  **1,007 distinct mapped pack files** decoded and uploaded through the
  production upload function in an ANGLE/Metal probe without failures. This
  checks file decoding and GPU acceptance, not every replacement's appearance.
- HD Texture Size limits replacements to 1024, 2048 or 4096 pixels (default
  2048), applied on the loader thread and on live setting changes. The option
  and its tests were integrated from the separate development worktree; a
  single-level compressed texture cannot discard a top mip and is unchanged.
- `black.py run --texture-audit` writes unique keys, dimensions, format and
  matched file name to that run's `texture-audit.csv`, without pixels. The
  diagnostic is off by default. The bounded second-mission audit quit normally
  with code 0 before its limit; 103 CTest checks, GPU probes and 30 Python checks
  passed. Each pack file stays on the GPU once used, so a level can hold
  300–800 MB of replacements.
- Not checked: the grenade crate after the alpha fix, missions after the second,
  long sessions, GPU memory on 8 GB Macs, and every mapped texture by eye. The
  map's pairs were matched by portingpete. The opt-in `--texture-dump` captures
  original top-level textures in the run's ignored output directory; it is
  bounded and is not enabled in normal play. The pack is never included in the
  repository or in downloads.
- Four additional Xbox keys are in `black-hd-reimagined-extra.map`: alternate
  encodings of the Boxes01 atlas and Recon_Secret screen. Their decoded disc
  textures were compared with the already mapped originals and HD layouts;
  the latter HD screen contains Russian text. A first-aid label, including the
  red cross, was matched from a key observed in a longer fourth-mission run.
  A door marked 1212 and its latch were matched to the same layout in the pack.
  These matches still need observation as rendered objects in gameplay. The
  extra file is loaded beside the chosen map; an available
  entry in the main map takes precedence. The upstream map is unchanged.

## Fourth-mission fixed-camera comparison

The separately developed HD size limit was incorporated before these runs.
With the same starting camera, 2× render scale, SMAA and MetalFX, scripted
muted runs on ANGLE/Metal measured 30.0 FPS without HD, 30.0 FPS at 1024
(118 MiB resident), and 29.7 FPS at 2048 (410 MiB resident). The 3× render
scale without HD measured 20.9 FPS at the same camera. Each run measures
about 26 seconds after initialization and exits normally. The native OpenGL
backend was 17.2 FPS in the same scene without HD (`run-nzbvhudr`); a process
sample found substantial time in GL draw and vertex buffer calls. The user's
long session above used OpenGL, but none of these tests reproduces its later
location; that location still needs a matched comparison.

## Fourth-mission later scene: live A/B

In a manual ANGLE/Metal run, the user reached the later location and kept the
same camera position and direction for approximately 14–15 seconds per
setting at render scale 2. Frame counts from the state trace give 28.5 FPS
with HD off, 30.0 at 2048 and 28.4 at 1024. After live toggling reloaded the
currently used pack textures, residency was about 316 MiB at 2048 and 105 MiB
at 1024; the initial 839 MiB at 2048 also included earlier areas. These are
short sequential measurements, not evidence that HD increases FPS. The
original 15 FPS was not reproduced on Metal at this view. An audit logged
map hits for Boxes01, the first-aid label, and door 1212; it does not prove
that each was visible or visually correct. The user subsequently exited normally
(exit 0, no timeout).

## Fourth-mission long session

- The user session recorded 324 unique eligible keys, 202 with map entries.
  `Boxes01` was resolved through the additional map. An audit hit identifies
  a lookup, not proof that an object was visible or looked correct.
- Game frame reports declined from about 30 FPS to 12–16 FPS around a rise in
  replacement residency from 621 to 880 MiB. This is a correlation; the log
  does not separate scene complexity, rendering, CPU, or memory pressure.
- The session ended with SIGABRT, not a timeout. Invalid GPU command data
  preceded a PROJECT2D texture with dimension 0, and its generated shader used
  an undeclared `t2`. A checked build overlay now emits the 2D fallback already
  selected by the sampler logic. A generator regression and GPU compilation of
  the captured shader with that declaration pass; this does not establish the
  cause of the invalid command data or validate the exact gameplay scene.

## Open work

- Longer combat/death/retry stress, additional missions and a complete playthrough.
- Remaining guest/interrupt/scheduler races and reached unresolved targets.
- Extended physical aiming/scopes/calibration, other controllers, analog trigger
  bindings, USB/Bluetooth hotplug, all control presets and Flash page layouts.
- Audio quality across weapons/environments and physical output-device changes.
- Long startup/gameplay frames. Neither stable 60 FPS nor unlocked-FPS physics
  is claimed; gameplay uses the original fixed step paced near 30 Hz.
- Clean-machine hardware testing beyond this Mac, lower macOS deployment targets,
  notarization, Intel, Linux and physical Steam Deck.
- New recomp settings inserted into the game's own Options/Pause Options pages;
  the native overlay is the current settings interface.

Game files and private saves/logs/captures are excluded from Git and downloads.
Per-run diagnostics and detailed development evidence remain local under `out/`.
