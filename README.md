<p align="center"><img src="resources/macos/AppIcon.png" alt="BLACK app icon" width="128"></p>
<h1 align="center">BLACK</h1>
<p align="center">An unofficial native Apple Silicon port of <i>BLACK</i> for Original Xbox.<br>Bring your own supported USA or European disc image.</p>
<p align="center"><a href="#download"><b>Download</b></a> · <a href="#quick-start">Quick start</a> · <a href="#highlights">Highlights</a> · <a href="#mods">Mods</a> · <a href="#controls-and-settings">Controls</a> · <a href="docs/BUILD.md">Build</a> · <a href="CHANGELOG.md">Changelog</a> · <a href="#acknowledgements">Acknowledgements</a></p>

> [!IMPORTANT]
> This is a non-commercial fan project, not affiliated with or endorsed by Electronic Arts,
> Criterion Games, Microsoft or Apple. No disc images, original executables, game assets,
> music, saves or generated game sources are distributed. You need your own supported
> Original Xbox BLACK disc. Please do not request game files or attach them to issues.

> [!NOTE]
> **0.5.4 is an experimental macOS release.** The title screen shows PRESS START,
> the copyright line and the logo again. From 0.5.3: HD textures no longer paint
> the grenade crate black or pickup weapons white. From 0.5.2: controllers can be
> unplugged and reconnected over USB or Bluetooth during play, a Bluetooth
> DualSense answers sooner after the desktop, and turning on HD textures checks
> for the pack and offers to choose its folder. Vibration, Controls, button prompts, setup,
> Russian text and subtitles from earlier releases remain included. Hotplug was
> physically checked on the development Mac with DualSense controllers; a
> complete playthrough, all runtime races, audio quality and other controller
> models remain unqualified. [Validation](docs/VALIDATION.md).

<p align="center">
  <img src="docs/screenshots/gameplay-firefight.jpg" alt="A firefight on an autumn street: an Uzi fires past a brick wall, leaves and smoke fly, an abandoned yellow car down the road" width="100%">
</p>

<table>
  <tr>
    <td width="50%"><img src="docs/screenshots/gameplay-courtyard.jpg" alt="A burning courtyard seen past a G36, crates stacked by a warehouse"><br><sub>A burning courtyard.</sub></td>
    <td width="50%"><img src="docs/screenshots/gameplay-muzzle-flash.jpg" alt="Muzzle flash fills a dark corridor as the rifle fires"><br><sub>Muzzle flash in a dark corridor.</sub></td>
  </tr>
  <tr>
    <td width="50%"><img src="docs/screenshots/gameplay-interior.jpg" alt="Inside a ruined house by tall windows, a bullet spark outside"><br><sub>The first mission, by the windows.</sub></td>
    <td width="50%"><img src="docs/screenshots/gameplay-debris.jpg" alt="Debris and smoke burst from a wall hit by gunfire"><br><sub>Walls break apart under fire.</sub></td>
  </tr>
</table>

<sub>Unedited macOS screenshots from this port on an M1 Pro, 2× render scale with SMAA and
MetalFX. The game content in them belongs to its owners.</sub>

## Download

**[BLACK 0.5.4 — macOS / Apple Silicon](https://github.com/secretpray/Black/releases/tag/v0.5.4)**

| Download | Purpose |
| --- | --- |
| `BLACK-0.5.4-macos-arm64.dmg` | App for Apple Silicon and **macOS 27 or later** |
| `SHA256SUMS` | Optional download integrity check |
| `optional-library-sources.tar.xz` | Runtime/library sources and build recipes; not needed to play |

Bundled libraries currently require macOS 27+. Older macOS versions, Intel Macs,
Linux and Steam Deck are not qualified. GitHub's automatic **Source code** downloads
are source snapshots, not playable apps.

## Quick start

<p align="center">
  <img src="docs/screenshots/setup-drag-to-applications.jpg" alt="The download's Finder window: BLACK FOR MAC in worn gold capitals, the app icon, an arrow to the Applications folder and DRAG BLACK TO APPLICATIONS" width="60%">
</p>

1. Open the DMG and drag **BLACK.app** to **Applications**.
2. Open **BLACK** from Applications or your app launcher.
3. First-time setup, drawn like the game's own menus, asks for your own
   **Original Xbox BLACK ISO/XISO** or extracted disc folder: choose it or drop it
   onto the window. It shows the release, languages and size before copying,
   then copies into your Library with progress; **Stop** keeps what was copied and
   the next install carries on from there. Your source stays unchanged. The
   supported European executable is normalized in that private copy. Then pick
   the game text from your disc's languages and the graphics settings; they
   apply on the first start. Use the mouse or keyboard; controller navigation
   is built in but not yet tested with a physical pad.
4. **F1/F10**, **Cmd+,** or **Back + Start** opens recomp settings.

The app is **ad-hoc signed, not notarized**. If macOS blocks it, use
**System Settings → Privacy & Security → Open Anyway** after checking the download source.
No Homebrew, Python or development checkout is required to run the DMG app.

Supported USA executable SHA-256:
`df2739c372d254a90aeeccf5971d12097f22d89fbb90db021fda96dd4deb68f6`.
Supported European executable SHA-1:
`796ad8369354e48aa15644d4707dd70bd3bc3b4c`.
PS2 and Xbox 360 files are not supported.

## Highlights

- Native ARM64 game code, NV2A rendering through ANGLE Metal and the DSP audio path.
- Saved 1–6× render scale, camera FOV, SMAA/FXAA, anisotropic filtering and MetalFX
  spatial upscaling. Graphics changes take effect on the next launch.
- A panel in the game’s own green-grey menu style, using fonts from your own game.
  The panel has **Port Settings**, **Controls** and **Mods** tabs,
  with device selection first and separate input subpages.
- An optional **FPS counter** showing the game's own frame rate.
- Optional **HD texture replacement** from your own copy of the PCSX2 "BLACK HD
  Reimagined" pack, through portingpete's map: turn on **Port Settings →
  Textures** and choose the pack's folder when asked. BLACK checks it and links
  it into `~/Library/Application Support/BLACK/Textures/replacement/` without
  copying (files placed there yourself work too). **HD Texture Size** limits the
  largest side to 1024, 2048 (default) or 4096 pixels. The pack is not included.
- Game text in English, French, German, Spanish or Italian from your disc, or
  Russian: the app carries the translation text only and builds the Russian
  bank and Cyrillic letters into your own installed copy when the game starts,
  and shows Russian subtitles in the mission briefing films.
  The settings panel has English, French, German, Spanish, Italian and Russian.
- Separate stick/trigger deadzones, explicit drift measurement, live axes,
  normal/aim X/Y sensitivity, hold-to-aim, aim assist and rumble intensity.
- Direct mouse look, two bindings per action, Flash menu cursor navigation
  and keyboard profile-name entry.
- Console-style gameplay timing around 30 updates per second. Unlocked-FPS
  physics and guaranteed 60 FPS are not advertised.
- Native fullscreen and Game Mode opt-in. Activation was observed on the test Mac;
  macOS controls it. FPS and controller latency improvements were not measured.
- Separate installation, settings, saves and per-session logs.

## Mods

<p align="center">
  <img src="docs/screenshots/settings-mods.jpg" alt="Settings, Mods page: GAME USA XBE, Films with Skip Intro Movies, and Player with God Mode, Infinite Medkits and Infinite Grenades" width="85%">
</p>

The **Mods** page collects cheats and modifications, all **off by default**:

| Group | Mod | What it does |
| --- | --- | --- |
| Films | Skip Intro Movies / Skip Mission Intros | Skips the startup films and mission briefings |
| Player | God Mode | Refills your health and holds the game's own invulnerability flag, so no hit gets through |
| Player | Infinite Medkits / Infinite Grenades | Keeps three medkits (the game's limit) and nine grenades |
| Player | Infinite Ammo | Your magazine and reserve never go down, with any weapon you hold |
| Player | Silver Weapons | The game's own Silver Weapons reward: magazines refill, 999 shown |
| Player | Rapid Fire | Your weapon fires 2, 3 or 4 times as fast (at most one shot a frame) |
| Player | No Recoil | Your shots no longer kick the view up |
| Player | One-Hit Kill | Any hit of your bullets kills a soldier |
| Player | Game Speed | 50%, 75%, 150% or 200% of the game's pace in play; menus and films keep theirs |
| Player | Always Silenced | Fits a silencer everywhere the old +9 trainer did (experimental) |
| Unlocks | Unlock All Missions / Unlock Black Ops Difficulty | Opens every mission and the Black Ops difficulty |
| Unlocks | M16A2 at Mission Start | The Black Ops reward rifle as your starting weapon |
| Unlocks | M249 in the First Mission | The developers' code, the same as a `XXXX-XXXX-XXXX-XXXX` profile name: an M249 with 600 rounds instead of the first mission's rifle; the gun has no texture there |

Rapid Fire, One-Hit Kill, No Recoil, Game Speed and the stronger God Mode
come from [TGP482/BLACK-XBERecomp#141](https://github.com/TGP482/BLACK-XBERecomp/pull/141)
(ported there from Black-Android). Silver Weapons and the M16A2 apply from the
next mission start or restart. Unlocks are
saved into your profile, and turning a mod off never relocks earned progress.
Addresses are kept per game revision; the page shows the detected one, and an
unrecognised executable leaves every mod off.

<table>
  <tr>
    <td width="50%"><img src="docs/screenshots/mods-in-game.jpg" alt="In a mission with mods on: three medkits, nine grenades, 999 rounds, the M16A2 and FPS 29.9 in the corner"><br><sub>Mods in play, with the FPS counter.</sub></td>
    <td width="50%"><img src="docs/screenshots/settings-mods-unlocks.jpg" alt="Mods page with No Recoil, One-Hit Kill, Game Speed, Always Silenced and Unlock All Missions"><br><sub>Player mods and unlocks.</sub></td>
  </tr>
</table>

## Controls and settings

| Action | Default |
| --- | --- |
| Recomp settings | F1 / F10 / Cmd+, / Back + Start |
| Change settings page | Q / E, Tab, LB / RB |
| Fullscreen / quit | Cmd+Return / Cmd+Q |
| Move / crouch / walk | W A S D / C / Alt |
| Fire / aim / reload / interact | Left mouse / right mouse / R / F |
| Melee / grenade / silencer / medkit | V / G / T / H |
| Change weapon | Q / E or mouse wheel |
| Objectives / pause | Tab / Escape |
| Game menus | Click / wheel / arrows; Enter confirms, Escape or right mouse goes back |

<table>
  <tr>
    <td width="50%"><img src="docs/screenshots/settings-recomp.jpg" alt="Current game-style Recomp Settings: render scale, MetalFX, field of view, FPS counter and SMAA"><br><sub>Recomp Settings: scale, upscaling, FOV, FPS counter.</sub></td>
    <td width="50%"><img src="docs/screenshots/settings-controller.jpg" alt="Gamepad and Vibration subpage: independent X/Y response, aiming and vibration"><br><sub>Gamepad response on its own page.</sub></td>
  </tr>
</table>

Open **Controls**. Device and prompt
selection are the first two rows, followed by **Button Bindings**, **Mouse and
Aiming**, and **Gamepad and Vibration**. [Input details and checks](docs/INPUT.md).

| Setting | Behaviour |
| --- | --- |
| Input Device | **Auto**, **Gamepad** or **Keyboard**. Auto starts with a connected pad; a deliberate key or mouse button can take over after the pad settles. Mouse motion alone leaves the pad in control. |
| Button Prompts | **Auto**, **Xbox**, **PlayStation** or **Keyboard**, independent of input selection. Auto follows the active device and SDL's controller family. |
| Controller Bindings | Reassign the 16 Xbox button/trigger controls. Select a row, press Enter, release all pad buttons, then press one. Escape cancels; Delete clears. Left/right also select a button. |

**Button Bindings → Edit Bindings For** selects Keyboard and Mouse or Gamepad
without changing gameplay input. Keyboard and mouse actions keep two bindings each.
Mouse and pad sensitivity live on their own pages as independent Look/Aiming
X/Y pairs. Deadzones, driver selection and live calibration are under
**Gamepad and Vibration → Advanced Gamepad**.
Game hints use the current binding. PlayStation buttons, shoulder/trigger
buttons, sticks, keyboard caps and mouse buttons use drawn pictures, shared
with the settings panel. The app prepares them in a private per-run view,
leaving source fonts intact. Missing font support falls back to button names.
Switching from pad to keyboard restores mouse look during gameplay. Game menus keep their physical confirm/back
buttons; gameplay rebinding is for BLACK's default control scheme. Settings
remain accessible by keyboard even with Gamepad selected and no pad connected.
Reset buttons affect the current page or binding list, preserving device choice,
prompt style and other pages. Back returns to the previous page and focus;
capture or axis editing is cancelled first.

<table>
  <tr>
    <td width="50%"><img src="docs/screenshots/settings-device.jpg" alt="Controls home: device and button prompts first, then bindings and response subpages"><br><sub>Choose gameplay input and button prompts independently.</sub></td>
    <td width="50%"><img src="docs/screenshots/settings-bindings.jpg" alt="Unified button editor set to Gamepad, with drawn PlayStation button pictures"><br><sub>Rebind the pad; prompts follow the selected family.</sub></td>
  </tr>
</table>

<sub>Settings captured from the running game window, with its title bar removed,
resized and compressed. The diagnostic run used scripted input, with no physical
controller connected; these images do not qualify hardware behaviour.</sub>

Click the game to capture the mouse in play. Escape releases it and pauses;
game menus release it automatically. Mouse motion alone does not take control
away from a gamepad. Measure drift with the sticks released, then test the
smallest comfortable deadzone on your own controller. Bluetooth DualSense
vibration/on-off, one Reload button rebind, keyboard/mouse handoff, mouse fire,
input after desktop return and USB/Bluetooth disconnect/reconnect (including
two DualSense controllers) have been checked on the development Mac. Other
controllers, trigger rebinds and broader calibration still need acceptance
testing.
A manual Gamepad choice selects the controller input path; selecting a particular
pad among several connected devices is not implemented.

## Your data and updates

Data is stored in `~/Library/Application Support/BLACK/`:

| Folder/file | Purpose |
| --- | --- |
| `Game/` | Your privately installed game files |
| `Data/xboxrecomp/` | Xbox saves, metadata and virtual cache partitions |
| `settings.ini` | Recomp preferences and bindings |
| `Logs/run-*/` | A separate log for every launch |

Replacing the app in Applications leaves Library data intact. Back up `Data/`
before changing save files. Development launcher data under `out/macos/user/`
is separate and is not automatically migrated or deleted.

## Building and reporting issues

[Build instructions](docs/BUILD.md) cover pinned sources, revision checks and packaging.
The root tree/history retains the upstream Windows implementation; its D3D12
features do not describe this Mac build. [Upstream README](docs/UPSTREAM_README.md).

Include the version, Mac model, macOS version, settings and reproduction steps
when reporting a problem. Distinguish a normal exit from a time limit.
Do not attach game data, private saves or unredacted personal information.

## Acknowledgements

Special thanks to **[TGP482 / BLACK-XBERecomp](https://github.com/TGP482/BLACK-XBERecomp)**
for the Original Xbox BLACK recompilation project, Xbox XBE Recomp tooling,
settings/menu design, control policies and the checked European revision patch.

Special thanks to **portingpete** and **[Black-2006-Recomp](https://github.com/portingpete/Black-2006-Recomp)**:
its launcher is the model for this port's first-time setup and its menu look,
its Xbox-to-pack map drives HD texture replacement, and its notes on the string
banks, the game fonts and save profiles, its audio completion work and its film
timing research guided much of the work here. Thanks as well for xboxrecomp.

Thanks to **soldieg** for [BLACK-SUBTITLES](https://github.com/soldieg/BLACK-SUBTITLES),
the source of the Russian film subtitles.

Thanks to **elpetese** for the [native macOS game host](https://github.com/elpetese/black-macos-recomp)
and [runtime](https://github.com/elpetese/black-macos-runtime), **sp00nz** for
xboxrecomp and BLACK runtime work, the **xemu** team for Xbox
hardware/rendering/audio foundations, **Matt Borgerson** for dsp56300, and the
**ANGLE**, **SDL** and other library contributors.

The [Burnout Revenge project](https://github.com/secretpray/burnout-revenge) served
as a product and packaging reference. BLACK keeps its own code, runtime ABI and data.
See [component provenance](THIRD_PARTY_NOTICES.md) and the app's
`Contents/Resources/Licenses/` for upstream licenses. The combined executable
contains differently licensed components and is not described as wholly MIT licensed.
