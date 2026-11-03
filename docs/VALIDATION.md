# Release validation and current limits

Candidate: **BLACK 0.3.0**, Apple Silicon. Test machine: Apple M1 Pro,
macOS 27.0.1. This is an experimental release, not a complete-game qualification.

## Evidence

- The user confirmed combat and game menus after the NV2A FIFO handoff fix.
  A 600-second session ended at its time limit without a forced kill or observed
  crash/hang. That timeout is not a new gameplay or quit PASS.
- The user has repeatedly confirmed Cmd+Q. Separate automated local quit checks
  also exited with code 0 before their limits and left no child process.
- The user's profile was saved and the next process read the full 6,144-byte
  Profile successfully. Restoring every checkpoint/completed-mission result has
  not been independently qualified.
- **100 CTest checks**, framebuffer/D3DCOLOR GPU probes and **30 Python checks**
  passed. These include native-C hook math, register preservation, race negative
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

## First-time setup (after 0.3.0)

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
- German, Spanish and Italian (after 0.3.0, same build):
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
- Physical mouse aiming, scopes, pad/keyboard handoff, calibration response,
  rumble, hotplug, focus changes and all Flash page layouts.
- Audio quality across weapons/environments and physical output-device changes.
- Long startup/gameplay frames. Neither stable 60 FPS nor unlocked-FPS physics
  is claimed; gameplay uses the original fixed step paced near 30 Hz.
- Clean-machine hardware testing beyond this Mac, lower macOS deployment targets,
  notarization, Intel, Linux and physical Steam Deck.
- New recomp settings inserted into the game's own Options/Pause Options pages;
  the native overlay is the current settings interface.

Game files and private saves/logs/captures are excluded from Git and downloads.
Per-run diagnostics and detailed development evidence remain local under `out/`.
