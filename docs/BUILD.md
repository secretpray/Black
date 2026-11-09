# Building BLACK for macOS

The root tree preserves TGP482's Windows source/history. macOS builds use the
native-C game/runtime submodules selected by `cmake/BlackMacOS.cmake`.
Do not mix TGP's C++ X86Context hooks with their generated C ABI.

## Requirements

Apple Silicon, a current Xcode installation with the Metal toolchain,
Homebrew, Python 3.11+, and your own supported Original Xbox BLACK disc.
The distributed 0.6.1 app requires macOS 27+ because of its actual library
build targets. A deployment-target flag alone does not establish older-OS support.

```sh
git clone --branch Black --recurse-submodules https://github.com/secretpray/Black.git
cd Black
python3 tools/macos/black.py setup
python3 tools/macos/black.py doctor
python3 tools/macos/black.py check --graphics
python3 tools/macos/black.py generate "/path/to/your/BLACK.iso"
python3 tools/macos/build_angle.py
python3 tools/macos/black.py build --real-dsp
python3 tools/macos/black.py run --game-mode --audio-engine dsp --dsp-headroom 6
```

`build` and `run` use the ANGLE Metal renderer by default (`--metal` is still
accepted). `--opengl` selects the legacy OpenGL build in `out/build/macos-arm64`
for comparisons only: in the same scene it ran at 17 FPS where Metal held 30.

An extracted disc directory can replace the ISO argument. The launcher checks
the XBE revision, preserves the original and normalizes the supported European
revision in a new local view. It also creates a local MainUS → MainUK strings alias.
Never commit ISO/XBE/assets/generated C, saves, captures or runtime logs.

## Source pins and overrides

| Component | Revision |
| --- | --- |
| Root TGP BLACK base | `10b8464fa06ebd6e54e814b250e9e3554a48ef1c` |
| Native game | `84ea2c7e803ca41ae6d38ed2b593efa445c236fd` |
| Native runtime | `46b2d4e6849a2ac57d93d6ee35115f76a4735327` |
| DSP56300 | `c60aeedb6a6014850204c71e53bc0cf5b3104222` |
| ANGLE | `56e0454d0d34942c7936230830aeccccbb994a2f` |

`black.py` refuses unexpected submodule pins or tracked submodule modifications.
Local game/runtime fixes live in `ports/macos/overrides/`; CMake creates checked
build-local overlays. Change pins explicitly when maintaining a modified runtime.
No source disc/XBE is patched in place. Build products live under ignored `out/`.

## Checks

```sh
python3 tools/macos/black.py check --graphics
python3 -m unittest discover -s tools/macos -p 'test_*.py' -v
python3 tools/macos/black.py run --windowed --mute \
  --test-quit-after 30 --smoke-seconds 45 --backup-saves
```

Bound automated runs and retain their unique log/outcome. A timeout is a stop
condition, not gameplay acceptance. Muted/dummy audio does not qualify real audio;
scripted input does not qualify a physical controller or rumble/hotplug.

## Input device and prompts

Controls saves Auto/Gamepad/Keyboard and a separate prompt family at the top
of its five-row home page. Bindings and mouse/gamepad response use subpages.
`run --input-device auto|gamepad|keyboard` explicitly overrides the saved device
for that run; without it the saved selection applies. Other preference writes
do not persist the temporary override. Editing Input Device clears it.

At launch the host builds a private `input-view-*` under the unique run folder:
all disc content is linked read-only by convention, and only Big.bin/Small.bin
are copied and given private-use bitmap button records. The expanded artwork
stays in a host atlas; guest texture allocation and source glyph pixels are
preserved. CoreGraphics draws the shared original button artwork.
The original font files, XBE and banks stay unchanged. The view is removed on a
normal return; a killed process may leave it in the ignored/private run folder.
Missing or unsupported fonts leave the game usable with spelled-out button names.
[Input behaviour and checks](INPUT.md).

## Russian game text

`resources/translations/ru/strings.json` holds the Russian text by string ID
(`null` keeps the game's English string) and ships in the app as
`russian-strings.json`. When Russian is chosen, before each start the app
(`game_translation.m`) prepares the installed copy: it keeps the English bank as `MainEn.bin` and the
retail fonts as `*.retail.bin`, adds А-Я and Ё to `Big.bin`/`Small.bin` drawn
from macOS's Impact and Arial Black at the game's capital height and width,
writes `MainRu.bin`, and makes `MainUS.bin` the Russian or English bank for the
chosen language. The game runs as English (language word 1).

The fonts' glyph records before the default glyph (header `0x1C`) are sorted by
`(code & 127, code)`, and the 128 words at `0x20` point at the first record of
each bucket or at the default glyph; the game finds glyphs only through them, so
new records are sorted in and the buckets rebuilt. Only the retail font hashes
are changed. Outside the app, `BLACK_RUSSIAN_STRINGS` names the JSON.

With Russian chosen the app also sets `BLACK_SUBTITLE_DIR` to its
`russian-subtitles` folder (outside the app, `subtitles/` beside the JSON) unless
a folder was named, so the briefing films show `resources/translations/ru/subtitles`.

`russian_font.py` is older: it rebuilds a fan translation's own font, which
paints Cyrillic over Latin cells. The app does not use it.

## App and DMG

```sh
python3 tools/macos/make_icon.py
python3 tools/macos/package_app.py out/build/macos-metal/black-recomp \
  out/release/0.6.1/BLACK.app --version 0.6.1
.venv/macos/bin/python3 -m pip install -r tools/macos/requirements-package.txt
.venv/macos/bin/python3 tools/macos/styled_dmg.py \
  out/release/0.6.1/BLACK.app out/release/0.6.1/BLACK-0.6.1-macos-arm64.dmg
python3 tools/macos/package_sources.py out/release/0.6.1/BLACK.app \
  out/release/0.6.1/optional-library-sources.tar.xz --version 0.6.1
```

Use an unused output path: packaging refuses to overwrite a prior candidate.
The app bundles transitive non-system dylibs, rewrites load paths, includes
SDL2-compat's loader-relative SDL3 alias, checks arm64/minimum OS, and signs
the libraries and app ad-hoc. It is not notarized.

The app's first-run setup (`app_setup.m`) copies an owned disc into Library data, validates
the original/normalized executable hashes and checks the European patch hash.
It rejects unsafe/cyclic XDVDFS structures and never replaces an existing install.
The source ISO/folder is opened for reading only. A stopped copy stays in
`.install-partial` beside `Game`, marked with the source, its executable hash and
file list, and is reused only for that same source; each file is written through
a `.part` file, so a complete name is a complete file. Other failures remove it.

`black-setup-preview <disc folder or image> <output>` draws every setup page to
PNG, checks keyboard selection, then stops a real copy of your disc at a third
and resumes it through the setup's own flow into `<output>/data`.

The optional source archive includes exact runtime/game-host/DSP source snapshots,
local overlays, library source archives and Homebrew recipes. ANGLE's official
pin/dependencies/build flags are in `build_angle.py` and the source manifest.
To relink modified static libraries, regenerate game code from your own disc
and rebuild with the modified runtime pin. For a compatible replacement dylib,
retain its `@rpath` identity in `Contents/Frameworks/`, then re-sign the dylib
and app with `codesign --force --sign -`. Reverse engineering needed to debug
modifications to LGPL components is permitted. Upstream licenses still apply.

The Mac settings overlay is inspired by TGP. Guest Flash cursor/text entry is
adapted in native C; inserting new recomp pages into Options/Pause Options is
still separate work. Renderer-only Windows features are not enabled by this build.
