# Release validation and current limits

Candidate: **BLACK 0.2.0**, Apple Silicon. Test machine: Apple M1 Pro,
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

## Languages (in development)

Not in the 0.2.0 download. The USA executable picks its string bank
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

## HD texture replacement (in development)

Not in the 0.2.0 download. **Recomp Settings → Textures → Texture Replacement**
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
- Not checked: other missions, long sessions, GPU memory on 8 GB Macs, and every
  mapped texture by eye. The map's pairs were matched by portingpete. Texture
  dumping is not implemented. The pack is never included in the repository or
  in downloads.

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
