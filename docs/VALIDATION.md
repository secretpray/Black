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
