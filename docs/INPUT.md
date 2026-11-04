# Input devices and button prompts

Added in **BLACK 0.5.0**; updated in **0.5.1**, 4 November 2026.

Open settings with F1/F10, Cmd+, or Back+Start (Share+Options), then choose
**Controls**. Its five rows fit without scrolling:

1. **Input Device** and **Button Prompts** are at the top.
2. **Button Bindings** opens one editor. **Edit Bindings For** switches between
   Keyboard and Mouse (two keys per action) and Gamepad (16 button/trigger controls).
   This editor choice does not change the gameplay input device.
3. **Mouse and Aiming** holds mouse response and behaviour.
4. **Gamepad and Vibration** holds stick response, aiming and vibration.
   **Advanced Gamepad** contains zones, aim assist and driver settings;
   **Controller Diagnostics** opens the live stick display and drift measurement.

The status above the tabs shows the saved mode and active device. Auto input
changes do not move focus or open a different page. Entering Controls starts
on its five-row home page. Back restores the row that opened a subpage; it
first cancels binding capture or finishes axis editing before going up.

| Setting | Choices |
| --- | --- |
| Input Device | Auto, Gamepad, Keyboard |
| Button Prompts | Auto, Xbox, PlayStation, Keyboard |
| Controller Bindings | Reassign or clear the 16 logical Xbox button/trigger controls |

Auto starts with a connected pad. A deliberate key, mouse button or wheel can
switch to keyboard after the pad has settled for 750 ms; mouse motion alone
does not take it over. Pad input takes control back. Disconnect returns Auto to
keyboard; reconnect starts with the pad. Switching to keyboard in gameplay or
closing settings restores relative mouse look. Switching to the pad releases the
mouse; Escape and focus loss release capture. Returning to the game window
restores keyboard/mouse capture and the correct first responder immediately.
Old menu suppression is cleared on foreground/device changes. Gamepad selects port 0's controller
path, not a particular model from a list of several connected pads. With no pad,
manual Gamepad keeps gameplay neutral; the keyboard still opens settings.

Button Prompts is independent of Input Device. Auto follows the active input
owner and SDL controller type: PS3/PS4/PS5 use PlayStation labels; other pads use
Xbox positions. An explicit prompt choice changes the labels only.

## Rebind a controller button

1. Select a row, then press Enter or physical A/cross; the value field also
   accepts a mouse click.
2. Release every button and trigger, then press one. The opening press is ignored.
3. Escape cancels. Delete clears (Fn+Backspace on a Mac keyboard). Left/right
   also select a source without capture.

Disconnect, a changed device instance, focus loss, page change or closing the
panel cancels capture without changing the binding. The finishing press is
consumed until release. Repeated sources are allowed across controls; another
binding is not silently swapped. Analog triggers keep their travel when used
as an analog source; digital sources produce 0/255. Stick axes keep the existing
sensitivity/deadzone settings and are not part of this button table.

Gameplay uses the new bindings. Game menus and the settings panel keep physical
confirm/back/navigation buttons, so unbinding A/B/Start cannot block settings.
Action labels assume BLACK's default control scheme; another in-game preset
needs separate testing. Keyboard/mouse retain two slots for each of 24 actions.

Mouse and gamepad response pages each show **Look** and **Aiming** as X/Y pairs.
Left/right choose an axis; Enter begins adjustment, left/right change its value,
and Enter or Back finishes. Mouse clicks select either field and its arrows.
Existing unequal X/Y values are preserved; mouse, pad and aim remain independent.

Reset buttons name their scope. Reset Bindings affects only the edited device's
bindings; Reset Mouse affects mouse response/behaviour; Reset Gamepad affects
normal pad response and vibration; Reset Advanced affects advanced pad settings.
They preserve device policy, prompt style and other pages. The home page has no
blanket input reset. Changes save immediately.

## Prompts in the game

The native panel and guest menus follow the chosen family. Keyboard hints use
the current first binding, then the second if the first is empty, or an unbound indicator.
Gamepad hints use the physical source assigned to that logical button.
PlayStation labels include cross/circle/square/triangle, L1/R1/L2/R2,
L3/R3 and Options/Share. The settings use drawn button pictures: coloured face
buttons, shoulders, triggers, sticks, directional controls, keyboard caps and
mouse buttons. Default Xbox guest hints keep the native artwork.

Already formatted tutorial and weapon-pickup text has a bounded cache of its
source and last rendered value, checked against HUD ownership, message ID and
weapon identity. Device/binding changes reformat that text; a buffer replaced
by the game is not overwritten. Scratch guards and 200/64-unit UTF-16 limits
reject oversized updates. This does not replace the control-layout artwork in
the game's own Options pages.

Original vector artwork is rasterized by one renderer for both Cocoa hints and
the game's text atlas. A private per-run Big.bin/Small.bin view adds bitmap-cell
records with private-use codes. The expanded pictures remain in host memory;
the guest texture payload retains its original size so BLACK's startup file
heap can still load GlobData.bin. Exact texture bytes, size and format identify
the two host replacements, independently of the optional HD texture setting.
Original glyph pixels and pixel geometry are preserved, with sorted hash buckets
rebuilt. Source fonts, XBE and banks stay unchanged. No game or system font is
redistributed. Unsupported fonts fall back to readable button names. Normal exit
removes the private view; a crash can leave it in the private run folder.

Preferences persist immediately in `settings.ini`: `input_device=0..2`,
`button_prompts=0..3`, `pad_<control>=-1..15`. Missing fields keep identity
bindings and Auto; invalid values are ignored. An explicit
`black.py run --input-device ...` overrides the saved device for that run.
Other settings writes do not persist the override; editing Input Device clears it.

## Evidence and remaining acceptance

110 CTest checks, framebuffer/D3DCOLOR probes, 43 Python tests, native menu
preview and the ANGLE Metal/DSP build passed. New production-path checks cover
synthetic SDL routing/reconnect, mappings/capture, persistence/CLI, guest
string/Flash bounds, cached HUD handoff/pickup guards, cdecl stack behaviour,
font-directory symlinks, preserved source pixels/geometry, host atlas lookup
and unchanged guest texture allocation. A Cocoa event regression covers
pad-to-keyboard relative motion, switching inside settings, Escape and focus gates. Preview images at
960×600 and 640×480 were inspected.

With the new drawn artwork, `run-clo_onby` loaded the host atlases with HD
textures off, displayed PlayStation face discs on Difficulty Select and loaded
the first mission with readable objective text. Cmd+Q exited normally (0),
and the child exited. This checks rendering and loading, not physical input.

In an earlier isolated muted/scripted run, Difficulty Select displayed PlayStation
cross/circle; switching to Keyboard changed Pause Menu to ENTER/ESC without
restarting. The second mission loaded, with normal exit 0 before the external
limit. A later run with the cached-HUD wrappers loaded the first mission and
also exited normally. Its frame contained no tutorial/pickup caption, so their
live visual replacement is covered by synthetic tests, not that game capture.
The user subsequently confirmed physical device switching and changing hints.
The 0.5.1 fixes were then physically checked on the development Mac with a
Bluetooth DualSense and keyboard/mouse: camera movement after pad-to-keyboard
handoff, one Reload-to-Square rebind (then restored), controller vibration and
its menu off switch. After the subsequent menu/focus changes, the user confirmed
that speech no longer loops in the recomp menu, input returns immediately after
the desktop, and mouse look/fire work after switching devices. Runtime diagnostics
also show keyboard fire=255 reaching the game's output with capture=0/menu=0.

Version 0.5.0's native XInputSetState only completed requests without sending
motor values to SDL. Version 0.5.1 forwards the packed Xbox feedback and keeps
the game's own enable flags in sync with Controls after profile loads. Original
Options/Pause vibration changes update the port setting too. While settings
pause gameplay, game audio fades to silence; closing clears queued silence and
fades back in. Source XBE/fonts/banks and submodule pins stay unchanged.

Remaining acceptance: USB/Bluetooth hotplug, other controller models, analog
trigger rebinds, extended aiming/calibration, visible long-lived tutorial/pickup
transitions, all control presets/languages and clean-machine updates. Muted or
scripted checks do not qualify hardware or real audio. Full-game stability and
checkpoint restoration remain separate.

The implementation adapts [TGP prompts/Flash](https://github.com/TGP482/BLACK-XBERecomp/tree/10b8464fa06ebd6e54e814b250e9e3554a48ef1c/src/game)
and [portingpete's bounded HUD cache](https://github.com/portingpete/Black-2006-Recomp/blob/2041e5a97f1300dca128d51fc06e1ae32cea9f21/src/black_hud_input.inc)
to the pinned native-C ABI. [Burnout Revenge input](https://github.com/secretpray/burnout-revenge/blob/0a6751f1ced138a2b2caaa6d51b4e1a891f1ff0d/runtime/hid/input.cpp)
and [its drawn button pictures](https://github.com/secretpray/burnout-revenge/blob/0a6751f1ced138a2b2caaa6d51b4e1a891f1ff0d/runtime/gpu/pad_glyphs.cpp)
were read-only product references for device detection, prompt selection and
button artwork; its runtime and texture atlas were not copied. Submodule pins are unchanged.
