# Changelog

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
