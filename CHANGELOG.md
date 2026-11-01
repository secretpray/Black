# Changelog

## Unreleased

- **Game Language** in Recomp Settings → General: English, French, German,
  Spanish or Italian game text from your disc's own string banks, applied on
  the next launch. The European disc has all five; the USA disc has English.
- **Menu Language** for the settings panel: Auto (follows the game), English,
  French, German, Spanish, Italian or Russian. Labels are translated in every
  language, help lines in Russian; the game itself has no Russian text.
- Quitting no longer clicks in the speakers: game audio fades out over 20 ms
  and the device closes before the app exits.

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
