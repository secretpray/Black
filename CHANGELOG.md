# Changelog

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
