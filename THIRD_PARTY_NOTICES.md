# Source provenance

The root source tree is based on
[TGP482/BLACK-XBERecomp](https://github.com/TGP482/BLACK-XBERecomp),
commit `10b8464fa06ebd6e54e814b250e9e3554a48ef1c`.

The experimental macOS build uses these pinned submodules:

| Component | Commit | Notices |
| --- | --- | --- |
| elpetese/black-macos-recomp | `84ea2c7e803ca41ae6d38ed2b593efa445c236fd` | [MIT and derivation credits](ports/macos/game/LICENSE) |
| elpetese/black-macos-runtime | `46b2d4e6849a2ac57d93d6ee35115f76a4735327` | [MIT](ports/macos/runtime/LICENSE), [component notices](ports/macos/runtime/NOTICE), [additional licenses](ports/macos/runtime/LICENSES/README.md) |

The macOS runtime includes code originating in sp00nznet/xboxrecomp and xemu.
Local native-C adaptations of TGP's BLACK deadzone/timer/speaker policies,
controller look/rumble, direct mouse look/action bindings, Flash menu cursor/name entry and centered settings layout/palette (`src/common/menu.cpp`,
`src/game/mouse.cpp`, `src/game/name_entry.cpp`, `src/game/flash.h`, `src/common/mouse_keyboard.cpp`,
`src/common/game_info.cpp`, same root commit) and
sp00nznet/xboxrecomp v0.16.0 (`f0f8a461496a4f23d9b0aeaa284b8d70ba7ab82b`)
audio queue pacing and voice gain ramp live outside the pinned submodules.
The APU source overlays retain the original source headers and LGPL notices.
The one-shot FE trap latch follows sp00nznet/xboxrecomp
`e0d2525ccf4b73e036f90687a1e8a83d50540737`; wide ADPCM decode buffers follow
`b380f02167d659e55a89c84ea99f99d1cb66eab5` and the 32-block range in
`aabebed0b261a2614107dee9011a8aeb9673697c`. Inclusive EBO and the preserved
idle-trap payload/cursors in `apu_vp_frame.inc` follow the portingpete runtime
pin below. These VP/core adaptations retain the xemu LGPL-2.0-or-later terms.
The block resampler and playback-timed SSL/EOF completion in
`voice_resample.inc`, with TU-local state in `voice_resample_state.h`, also
follow that portingpete runtime pin's `src/apu/apu_vp.c`, under the same
xemu LGPL-2.0-or-later terms. The pinned APU state ABI is unchanged.
The two-phase voice queue/wait and pending IRQ publication in
`apu_vp_frame.inc` / `apu_publish_irq.inc` follow the same portingpete runtime
pin. The previous walker is retained in `apu_vp_frame_previous.inc` solely
for negative controls. These APU adaptations retain xemu LGPL-2.0-or-later
terms; they do not replace the kernel's interrupt/IRQL framework.
The FE gate / partial-epoch preservation in `apu_frame_thread.inc` and immediate
APU IRQ wake policy are adapted from
[portingpete/xboxrecomp](https://github.com/portingpete/xboxrecomp/tree/a40397f9baeceb6970fb3b484d93fe9d49925d91),
commit `a40397f9baeceb6970fb3b484d93fe9d49925d91` (`src/apu/apu_core.c` and
`src/kernel/kernel_bridge.c`). The APU adaptation retains the source's
xemu LGPL-2.0-or-later terms; the kernel bridge retains its runtime license.
The game completion guard and embedded-list coalescing in `audio_completion.inc`
are adapted from the MIT-licensed
[portingpete/Black-2006-Recomp manual source](https://github.com/portingpete/Black-2006-Recomp/blob/2041e5a97f1300dca128d51fc06e1ae32cea9f21/src/recomp_manual.c).
Copyright (c) 2026 portingpete and sp00nz; the complete
[MIT permission notice](ports/macos/licenses/portingpete-MIT.txt) is retained.
The nonblocking POSIX HAL dispatch guard uses the existing pinned runtime lock;
its ownership policy follows the runtime donor above without replacing its ABI.
The optional real DSP engine reuses the pinned runtime's xemu DSP adapter
([upstream snapshot and notices](ports/macos/runtime/src/apu/dsp/UPSTREAM.md)),
GPL-2.0-or-later / LGPL-2.0-or-later, and the MIT-licensed
[mborgerson/dsp56300](https://github.com/mborgerson/dsp56300/tree/c60aeedb6a6014850204c71e53bc0cf5b3104222)
ARM64 JIT at `c60aeedb6a6014850204c71e53bc0cf5b3104222`.
The output limiter is reused from the same pinned runtime's `apu_limiter.h`.
Its extracted NV2A renderer contains LGPL and GPL components; see its
[renderer CMake file](ports/macos/runtime/third_party/xemu_nv2a/CMakeLists.txt)
and adjacent COPYING/COPYING.LIB files. These components retain their licenses;
the combined executable must not be described as entirely MIT licensed.

The macOS presenter's optional SMAA pass compiles the SMAA reference shader and
its precomputed area/search lookups as the root tree already keeps them in
`src/d3d/smaa_shader.h` and `src/d3d/smaa_textures.h` (Copyright (C) 2013 Jorge
Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro, Diego Gutierrez;
MIT, permission notice retained in both headers). `ports/macos/overrides/smaa_metal.m`
only rewrites the shader text for Metal at run time; it ships no second copy.

BLACK is an Electronic Arts / Criterion Games title. This repository does not
include its disc image, executable, assets or generated game C/C++ sources.
The metadata archive remains in the original game submodule and contains
function addresses/call graphs/analysis rather than game instruction bytes.
Google ANGLE, SDL, GLib, libepoxy, xxHash and OpenSSL retain their own notices.
