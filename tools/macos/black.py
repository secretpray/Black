#!/usr/bin/env python3
"""Build/run the native macOS BLACK port. Game-derived output stays in out/."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile

import save_evidence
import game_bundle

ROOT = Path(__file__).resolve().parents[2]
GAME = ROOT / "ports/macos/game"
RUNTIME = ROOT / "ports/macos/runtime"
WORK = ROOT / "out/macos"
GEN = WORK / "generated"
VENV = ROOT / ".venv/macos"
PYTHON = VENV / "bin/python3"
PINS = {
    "ports/macos/game": "84ea2c7e803ca41ae6d38ed2b593efa445c236fd",
    "ports/macos/runtime": "46b2d4e6849a2ac57d93d6ee35115f76a4735327",
}
# NTSC-U executable also used by portingpete/Black-2006-Recomp. The native
# host has fixed entry/XDK addresses, so another revision must be ported first.
XBE_SHA256 = "df2739c372d254a90aeeccf5971d12097f22d89fbb90db021fda96dd4deb68f6"
EUROPE_SHA1 = "796ad8369354e48aa15644d4707dd70bd3bc3b4c"
ANALYSIS_FILES = (
    "tools/disasm/output/functions.json",
    "tools/disasm/output/labels.json",
    "tools/func_id/output/identified_functions.json",
    "tools/abi_analysis/output/abi_functions.json",
)


def run(args, **kwargs):
    print("+", " ".join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True, **kwargs)


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def check_sources():
    for name, expected in PINS.items():
        path = ROOT / name
        if not (path / "CMakeLists.txt").is_file():
            raise ValueError("Missing native sources. Run: python3 tools/macos/black.py setup")
        actual = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
        if actual != expected:
            raise ValueError(f"{name}: expected {expected}, got {actual}. Update the pins deliberately before rebuilding.")
        changes = subprocess.check_output(["git", "-C", str(path), "status", "--porcelain", "--untracked-files=no"], text=True)
        if changes:
            raise ValueError(f"{name} has tracked edits. Record a new source commit/pin before generating game code.")


def validate_xbe(path):
    path = Path(path)
    with path.open("rb") as stream:
        header = stream.read(0x180)
        if len(header) < 0x180 or header[:4] != b"XBEH":
            raise ValueError(f"Not an Xbox XBE: {path}")
        base = struct.unpack_from("<I", header, 0x104)[0]
        certificate = struct.unpack_from("<I", header, 0x118)[0]
        entry = struct.unpack_from("<I", header, 0x128)[0] ^ 0xA8FC57AB
        offset = certificate - base
        if offset < 0 or offset + 12 > path.stat().st_size:
            raise ValueError("Invalid XBE certificate offset")
        stream.seek(offset + 8)
        title = struct.unpack("<I", stream.read(4))[0]
    if title != 0x45410083 or entry != 0x25A3F:
        raise ValueError(f"Unsupported BLACK revision: title={title:08X}, entry={entry:08X}; native hooks target NTSC-U")
    sha = digest(path)
    if sha != XBE_SHA256:
        raise ValueError(f"Unsupported executable SHA-256: {sha}. Expected NTSC-U {XBE_SHA256}. No files were patched.")
    return sha


def apply_europe_patch(original, patch):
    """TGP's EuropePatch format, as implemented in src/common/game_files.cpp."""
    cursor = 0
    read = 0
    output = bytearray()

    def number():
        nonlocal cursor
        value = 0
        for shift in range(0, 64, 7):
            if cursor >= len(patch):
                raise ValueError("Truncated Europe patch")
            byte = patch[cursor]
            cursor += 1
            value |= (byte & 127) << shift
            if byte < 128:
                return value
        raise ValueError("Invalid Europe patch integer")

    while cursor < len(patch):
        count, changes = number(), number()
        if read < 0 or read + count > len(original) or len(output) + count > 16 * 1024 * 1024:
            raise ValueError("Europe patch copy outside executable")
        start = len(output)
        output.extend(original[read:read + count])
        at = 0
        for _ in range(changes):
            at += number()
            if at >= count or cursor >= len(patch):
                raise ValueError("Invalid Europe patch change")
            output[start + at] = (output[start + at] + patch[cursor]) & 255
            cursor += 1
            at += 1
        added = number()
        if cursor + added > len(patch) or len(output) + added > 16 * 1024 * 1024:
            raise ValueError("Europe patch extra outside patch")
        output.extend(patch[cursor:cursor + added])
        cursor += added
        move = number()
        read += count + (move // 2 if move % 2 == 0 else -(move // 2) - 1)
    return bytes(output)


def prepare_game(game_dir):
    xbe = game_dir / "default.xbe"
    original = xbe.read_bytes()
    if hashlib.sha1(original).hexdigest() != EUROPE_SHA1:
        validate_xbe(xbe)
        if game_dir == WORK / "normalized-game" and (WORK / "disc-provenance.json").exists():
            return game_dir, json.loads((WORK / "disc-provenance.json").read_text())
        return game_dir, {"region": "USA", "original_xbe_sha256": digest(xbe)}
    patch = ROOT / "src/common/europe.patch"
    normalized = apply_europe_patch(original, patch.read_bytes())
    if hashlib.sha256(normalized).hexdigest() != XBE_SHA256:
        raise ValueError("Europe patch result does not match the expected USA executable")
    # A local view: assets point at the extracted disc; only the normalized
    # executable is new. The supplied disc/folder is never patched in place.
    view = WORK / "normalized-game"
    if view.exists():
        raise ValueError(f"{view} already exists; use it as the generation input or move it aside first")
    view.mkdir()
    link_disc_assets(game_dir, view)
    (view / "default.xbe").write_bytes(normalized)
    record = {"region": "Europe", "original_xbe_sha256": hashlib.sha256(original).hexdigest(),
              "original_xbe_sha1": EUROPE_SHA1, "europe_patch_sha256": digest(patch),
              "asset_aliases": {"language/strings/MainUS.bin": "language/strings/MainUK.bin"}}
    (WORK / "disc-provenance.json").write_text(json.dumps(record, indent=2) + "\n")
    print("European XBE normalized to the verified USA revision in a local asset view; original files unchanged.")
    return view, record


def link_disc_assets(source, view, prefix=""):
    """Create a read-only-by-convention asset view, with a local English alias.

    The USA host asks for MainUS.bin; the European disc supplies MainUK.bin.
    Keep the two directory levels real so that adding the alias never writes
    through a directory symlink into the original extracted disc.
    """
    for entry in source.iterdir():
        relative = (prefix + entry.name).lower()
        if relative == "default.xbe":
            continue
        target = view / entry.name
        if relative in ("language", "language/strings") and entry.is_dir():
            target.mkdir()
            link_disc_assets(entry, target, relative + "/")
        else:
            target.symlink_to(entry.resolve(), target_is_directory=entry.is_dir())
    if prefix == "language/strings/":
        names = {entry.name.lower(): entry for entry in source.iterdir()}
        if "mainus.bin" not in names and "mainuk.bin" in names:
            (view / "MainUS.bin").symlink_to(names["mainuk.bin"].resolve())


def extract_analysis(archive, destination):
    # Ignore macOS AppleDouble records. Copy only the four known metadata
    # files; never let archive paths choose a destination on the filesystem.
    with tarfile.open(archive, "r:xz") as source:
        for name in ANALYSIS_FILES:
            member = source.getmember(name)
            if not member.isfile():
                raise ValueError(f"Not a regular metadata file: {name}")
            data = source.extractfile(member).read()
            json.loads(data)
            target = destination / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)


def setup(_args):
    run(["git", "submodule", "update", "--init", "ports/macos/game", "ports/macos/runtime"], cwd=ROOT)
    check_sources()
    if not shutil.which("brew"):
        raise ValueError("Homebrew is required for setup: https://brew.sh")
    run(["brew", "install", "cmake", "ninja", "pkgconf", "sdl2", "sdl3", "glib", "libepoxy", "xxhash", "openssl@3"], env={**os.environ, "HOMEBREW_NO_AUTO_UPDATE": "1"})
    if not PYTHON.exists():
        run([sys.executable, "-m", "venv", VENV])
    run([PYTHON, "-m", "pip", "install", "-r", ROOT / "tools/macos/requirements.txt"])


def doctor(_args):
    failures = []
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        failures.append("native Apple Silicon macOS required (run outside Rosetta)")
    for tool in ("git", "clang", "cmake", "ninja", "pkg-config"):
        path = shutil.which(tool)
        print(f"{tool}: {path or 'MISSING'}")
        if not path:
            failures.append(tool)
    try:
        check_sources()
    except (ValueError, subprocess.CalledProcessError) as error:
        failures.append(str(error))
    if shutil.which("pkg-config"):
        for lib in ("sdl2", "sdl3", "glib-2.0", "epoxy", "libxxhash", "openssl"):
            result = subprocess.run(["pkg-config", "--modversion", lib], capture_output=True, text=True)
            print(f"{lib}: {result.stdout.strip() or 'MISSING'}")
            if result.returncode:
                failures.append(lib)
    if not PYTHON.exists() or subprocess.run([str(PYTHON), "-c", "import capstone, unicorn"], capture_output=True).returncode:
        failures.append("Python generation dependencies: run setup")
    print("Game files: " + ("generated locally" if (GEN / "provenance.json").exists() else "not generated; runtime checks remain available"))
    if failures:
        raise ValueError("Missing prerequisites: " + "; ".join(failures))
    print("Native build prerequisites OK. This does not validate gameplay.")


def generate(args):
    check_sources()
    if not PYTHON.exists():
        raise ValueError("Run setup first to create the local Python environment")
    source = Path(args.source).expanduser().resolve()
    WORK.mkdir(parents=True, exist_ok=True)
    game_dir = source if source.is_dir() else WORK / "disc"
    if source.is_file():
        if source.suffix.lower() not in (".iso", ".xiso"):
            raise ValueError("Choose a full game folder or an ISO/XISO, not an individual XBE")
        if game_dir.exists():
            raise ValueError(f"Disc extraction already exists at {game_dir}; use that folder as input or move it aside first")
        run([PYTHON, "-m", "tools.xiso", "unpack", source, "-o", game_dir], cwd=RUNTIME)
    game_dir, disc_record = prepare_game(game_dir)
    xbe = game_dir / "default.xbe"
    sha = validate_xbe(xbe)
    # Generate in a fresh directory and publish only after the lifter succeeds.
    # Old usable output remains untouched on an interrupted/failed generation.
    with tempfile.TemporaryDirectory(prefix="generation-", dir=WORK) as temp:
        temp = Path(temp)
        extract_analysis(GAME / "analysis/black-analysis.tar.xz", temp)
        staged = temp / "generated"
        run([PYTHON, "-m", "tools.recomp", xbe, "--game-name", "Black", "--all",
             "--split", "1000", "--weak-feedback-seeds",
             "--output-dir", temp / "reports",
             "--exclude-manual", GAME / "src/recomp_natives.c",
             "--disasm-dir", temp / "tools/disasm/output",
             "--func-id-dir", temp / "tools/func_id/output",
             "--abi-dir", temp / "tools/abi_analysis/output", "--gen-dir", staged], cwd=RUNTIME)
        if not (staged / "recomp_types.h").exists() or not list(staged.glob("*.c")):
            raise ValueError("Lifter did not emit a complete generated source directory")
        record = {"xbe_sha256": sha, "game_dir": str(game_dir), "sources": PINS,
                  "analysis_sha256": digest(GAME / "analysis/black-analysis.tar.xz"),
                  "disc": disc_record}
        summary = temp / "reports/summary.json"
        if summary.exists():
            report = json.loads(summary.read_text())
            record["lifter"] = {key: report.get(key) for key in
                               ("total", "translated", "failed", "unresolved_stubs", "manual_functions", "unimplemented")}
            shutil.copy2(summary, staged / "lifter-summary.json")
        (staged / "provenance.json").write_text(json.dumps(record, indent=2) + "\n")
        backup = WORK / "generated.previous"
        if backup.exists():
            shutil.rmtree(backup)
        if GEN.exists():
            GEN.rename(backup)
        staged.rename(GEN)
    print("Game code generated locally. Next: python3 tools/macos/black.py build")


def provenance():
    check_sources()
    marker = GEN / "provenance.json"
    if not marker.is_file():
        raise ValueError("Generate the game from your disc first: python3 tools/macos/black.py generate /path/to/BLACK")
    data = json.loads(marker.read_text())
    if data["sources"] != PINS or data["analysis_sha256"] != digest(GAME / "analysis/black-analysis.tar.xz"):
        raise ValueError("Sources changed since generation; regenerate from your disc")
    if validate_xbe(Path(data["game_dir"]) / "default.xbe") != data["xbe_sha256"]:
        raise ValueError("Game executable changed since generation")
    return data


def add_renderer(sub):
    """ANGLE Metal unless --opengl: the OpenGL path measured 17 FPS where
    Metal held 30 in the same scene, so it is only for comparisons."""
    renderer = sub.add_mutually_exclusive_group()
    renderer.add_argument("--metal", dest="metal", action="store_true", default=True,
                          help="ANGLE Metal renderer (the default; kept for existing scripts)")
    renderer.add_argument("--opengl", dest="metal", action="store_false",
                          help="Legacy OpenGL renderer for comparisons; much slower in heavy scenes")


def preset(args):
    if args.metal:
        folder = os.environ.get("BLACK_ANGLE_LIB_DIR") or str(WORK / "angle/lib")
        if any(not (Path(folder) / lib).is_file() for lib in ("libEGL.dylib", "libGLESv2.dylib")):
            raise ValueError("Build ANGLE with python3 tools/macos/build_angle.py, or set BLACK_ANGLE_LIB_DIR to your arm64 libEGL.dylib/libGLESv2.dylib; --opengl uses the slower legacy renderer")
        os.environ["BLACK_ANGLE_LIB_DIR"] = str(Path(folder).resolve())
        return "macos-metal"
    return "macos-arm64"


def build(args):
    provenance()
    name = preset(args)
    run(["cmake", "--preset", name, f"-DBLACK_ENABLE_REAL_DSP={'ON' if args.real_dsp else 'OFF'}"], cwd=ROOT)
    run(["cmake", "--build", "--preset", name, "-j", args.jobs], cwd=ROOT)


def check(args):
    check_sources()
    run(["cmake", "--preset", "macos-runtime"], cwd=ROOT)
    run(["cmake", "--build", "--preset", "macos-runtime", "-j", args.jobs], cwd=ROOT)
    run(["ctest", "--preset", "macos-runtime"], cwd=ROOT)
    if args.graphics:
        run(["cmake", "--build", "out/build/macos-runtime", "--target", "black-graphics-check"], cwd=ROOT)


def stop_child(process):
    process.terminate()
    try:
        process.wait(timeout=5)
        return False
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
        return True


ANISO_LEVELS = (0, 2, 4, 8, 16)
AA_MODES = {"off": 0, "fxaa": 1, "smaa": 2}


def launch_graphics(path):
    """Read the persisted launch settings; explicit CLI flags win later."""
    values = {"render_scale": 2, "field_of_view": 70, "anti_aliasing": 2, "anisotropic_filtering": 0, "upscaler": 1}
    limits = {"render_scale": (1, 6), "field_of_view": (45, 140), "anti_aliasing": (0, 2),
              "anisotropic_filtering": (0, 16), "upscaler": (0, 1)}
    try:
        lines = path.read_text(errors="replace").splitlines()
    except FileNotFoundError:
        return values
    for line in lines:
        key, separator, raw = line.partition("=")
        key, raw = key.strip(), raw.strip()
        if separator and key in limits and len(raw) <= 15 and re.fullmatch(r"[+-]?[0-9]+", raw):
            value = int(raw)
            lo, hi = limits[key]
            if lo <= value <= hi and (key != "anisotropic_filtering" or value in ANISO_LEVELS):
                values[key] = value
    return values


def launch_state(args):
    """Keep repeatable save qualification away from the user's active data."""
    if not args.test_state_dir:
        return WORK / "user"
    if not args.smoke_seconds or not args.backup_saves or args.dev_unlock_levels is not None:
        raise ValueError("--test-state-dir requires --smoke-seconds and --backup-saves, without --dev-unlock-levels")
    state = Path(os.path.abspath(Path(args.test_state_dir).expanduser()))
    validation = (WORK / "validation").resolve()
    if state == validation or not state.is_relative_to(validation) or state != state.resolve():
        raise ValueError("--test-state-dir must be a separate directory below out/macos/validation, without symbolic links")
    # A nested data/ symlink could otherwise route XDG writes into live saves.
    for path in (state / "data", state / "data/xboxrecomp", state / ".run.lock", state / "settings.ini"):
        if path.is_symlink():
            raise ValueError(f"Test state refuses a symbolic link: {path}")
    if args.settings_file:
        settings = Path(os.path.abspath(Path(args.settings_file).expanduser()))
        if not settings.is_relative_to(state) or settings != settings.resolve():
            raise ValueError("Test settings must be inside --test-state-dir without symbolic links")
    return state


def play(args):
    state = launch_state(args)
    settings_path = Path(args.settings_file).expanduser().resolve() if args.settings_file else state / "settings.ini"
    graphics = launch_graphics(settings_path)
    if args.scale is None:
        args.scale = graphics["render_scale"]
    if args.fov is None:
        args.fov = graphics["field_of_view"]
    if args.aa is None:
        args.aa = {mode: name for name, mode in AA_MODES.items()}[graphics["anti_aliasing"]]
    if args.af is None:
        args.af = graphics["anisotropic_filtering"]
    if args.upscale is None:
        args.upscale = "metalfx" if graphics["upscaler"] else "off"
    if args.test_quit_after and (args.headless or not args.smoke_seconds or args.test_quit_after >= args.smoke_seconds):
        raise ValueError("--test-quit-after needs a visible bounded run and a quit time earlier than --smoke-seconds")
    if args.headless and (not args.smoke_seconds or not args.mute):
        raise ValueError("--headless requires a bounded --smoke-seconds run and --mute")
    if args.headless and args.capture:
        raise ValueError("--capture needs the presenter window; use logs/audio capture for headless checks")
    if args.headless and args.open_settings:
        raise ValueError("--open-settings needs a game window")
    if args.headless and args.game_mode:
        raise ValueError("--game-mode needs a visible native full-screen window")
    if args.script_only and not (args.input_script or args.test_input):
        raise ValueError("--script-only requires --input-script or --test-input")
    if args.gpu_readback == "auto":
        # Measured 31.10 on all eight missions: on-demand runs the game at its
        # 30 Hz step where the per-flip download held it at 12-23 fps. At 1x the
        # presenter reads guest RAM, so every surface must still come back.
        args.gpu_readback = "always" if args.scale == 1 else "on-demand"
    if args.gpu_readback == "on-demand" and args.scale == 1:
        raise ValueError("On-demand readback currently requires --scale 2 or higher (the native-size presenter reads guest RAM)")
    data = provenance()
    if args.test_game_view:
        if args.dev_unlock_levels is None or not args.smoke_seconds:
            raise ValueError("--test-game-view requires --dev-unlock-levels and --smoke-seconds")
        test_view = Path(args.test_game_view).expanduser().resolve(strict=True)
        if validate_xbe(test_view / "default.xbe") != data["xbe_sha256"]:
            raise ValueError("Test view executable does not match generated code")
        data = {**data, "game_dir": str(test_view)}
    name = preset(args)
    binary = ROOT / "out/build" / name / "black-recomp"
    if not binary.is_file():
        raise ValueError(f"Build {name} first")
    if args.audio_engine == "dsp":
        cache = binary.parent / "CMakeCache.txt"
        if not cache.is_file() or "BLACK_ENABLE_REAL_DSP:BOOL=ON" not in cache.read_text():
            opengl = "" if args.metal else " --opengl"
            raise ValueError(f"DSP is not built for {name}; run python3 tools/macos/black.py build{opengl} --real-dsp")
    # Separate local saves from source/game data and the TGP Windows build.
    state.mkdir(parents=True, exist_ok=True)
    with (state / ".run.lock").open("w") as guard:
        try:
            fcntl.flock(guard, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise ValueError("BLACK is already running through this launcher")
        if args.game_mode:
            binary, args.game_mode_bundle = game_bundle.prepare(binary)
        play_locked(args, settings_path, data, name, binary, state)


def play_locked(args, settings_path, data, name, binary, state):
    """Keep save backup, child lifetime and final inventory under one run lock."""
    run_dir = Path(tempfile.mkdtemp(prefix="run-", dir=state))
    env = {k: v for k, v in os.environ.items() if not k.startswith(("RECOMP_", "BLACK_"))}
    if args.texture_audit:
        env["BLACK_TEXTURE_AUDIT"] = str(run_dir / "texture-audit.csv")
    if args.subtitle_dir:
        env["BLACK_SUBTITLE_DIR"] = str(Path(args.subtitle_dir).expanduser().resolve(strict=True))
    if args.texture_dump:
        dump_dir = run_dir / "texture-dumps"
        dump_dir.mkdir(mode=0o700)
        env["BLACK_TEXTURE_DUMP_DIR"] = str(dump_dir)
    # The pinned host passes save_dir=NULL. Give its XDG fallback a BLACK-only
    # root instead of sharing ~/.local/share/xboxrecomp with other titles.
    dev = args.dev_unlock_levels is not None
    env["XDG_DATA_HOME"] = str(run_dir / "data" if dev else state / "data")
    if dev:
        # Qualification only: never let unlocked progress reach real saves.
        env["BLACK_DEV_UNLOCK_LEVELS"] = str(args.dev_unlock_levels)
    env["BLACK_SETTINGS_FILE"] = str(settings_path)
    env["BLACK_RUN_DIRECTORY"] = str(run_dir)
    env["BLACK_GAME_DIR"] = data["game_dir"]
    # CLI/game-mode runs pass argv[1], bypassing Finder's translation setup.
    # Native code prepares banks/fonts in its disposable per-run view.
    env["BLACK_RUSSIAN_STRINGS"] = str(ROOT / "resources/translations/ru/strings.json")
    # Texture replacement reads the pack from BLACK_TEXTURE_DIR/replacement
    # (default: ~/Library/Application Support/BLACK/Textures) through this map.
    env["BLACK_TEXTURE_MAP"] = str(ROOT / "resources/textures/black-hd-reimagined.map")
    if args.texture_dir:
        env["BLACK_TEXTURE_DIR"] = str(Path(args.texture_dir).expanduser().resolve())
    if args.open_settings:
        env["BLACK_OPEN_SETTINGS"] = args.open_settings
    if args.test_unit is not None:
        env["RECOMP_BLACK_TEST_UNIT"] = str(args.test_unit)
    if args.checkpoint_test:
        env["RECOMP_BLACK_CHECKPOINT_TEST"] = "1"
        env["RECOMP_BLACK_CHECKPOINT_TRACE"] = "1"
    # SDL otherwise turns SIGTERM into a queued SDL_QUIT event, which the
    # pinned input loop discards; a bounded run then needs SIGKILL.
    env["SDL_NO_SIGNAL_HANDLERS"] = "1"
    env.update(RECOMP_XEMU_GPU="1", RECOMP_VBLANK="1", RECOMP_AC97_READY="1",
               RECOMP_PB_EXEC="1", RECOMP_FB_WINDOW="1", RECOMP_KEYBOARD="0",
               RECOMP_SCALE=str(args.scale), RECOMP_FOV=str(args.fov), RECOMP_AA={"off": "0", "fxaa": "1", "smaa": "smaa"}[args.aa],
               RECOMP_AF=str(args.af), RECOMP_UPSCALE=args.upscale,
               RECOMP_ASPECT=args.aspect)
    if args.pad_deadzone is not None:
        env["RECOMP_PAD_DEADZONE"] = str(args.pad_deadzone)
    if args.game_clock == "console":
        env["BLACK_CONSOLE_GAME_CLOCK"] = "1"
    elif args.game_clock == "original":
        env["BLACK_ORIGINAL_GAME_CLOCK"] = "1"
    else:
        env["BLACK_REALTIME_GAME_CLOCK"] = "1"
    if args.audio_routing == "legacy":
        env["BLACK_LEGACY_AUDIO_ROUTING"] = "1"
    if args.audio_pacing == "legacy":
        env["BLACK_LEGACY_AUDIO_PACING"] = "1"
    if args.voice_gain == "legacy":
        env["BLACK_LEGACY_VOICE_GAIN"] = "1"
    if args.world_wait == "legacy":
        env["BLACK_LEGACY_WORLD_WAIT"] = "1"
    # This run's choice; the app itself restores the player's last one.
    env["RECOMP_FULLSCREEN"] = "0" if args.windowed else "1"
    if args.restore_window:
        del env["RECOMP_FULLSCREEN"]
    if args.skip_movies:
        env["BLACK_SKIP_MOVIES"] = "1"
    if args.skip_intros:
        env["BLACK_SKIP_INTROS"] = "1"
    if args.state_log:
        env["BLACK_STATE_LOG"] = "1"
    if args.save_trace:
        env["BLACK_SAVE_TRACE"] = "1"
    if args.diagnostics:
        env.update(RECOMP_FPSLOG="1", RECOMP_MENULOG="1", RECOMP_HANG_DUMP="30", RECOMP_XINLOG="1")
        # Keep the runtime's bounded native sample beside this run's guest dump.
        env["RECOMP_HANG_DIR"] = str(run_dir)
        env["BLACK_SYNC_LOG"] = "1"
        env["RECOMP_INPUT_DEBUG"] = "1"
        env["RECOMP_AUDIO_DIAG"] = "1"
    if args.frame_times:
        env["RECOMP_FPSLOG"] = "1"
    if args.profile_frames:
        traces = run_dir / "frame-trace"
        traces.mkdir()
        env["BLACK_FRAME_TRACE_DIR"] = str(traces)
        env["BLACK_FILM_LOG"] = "1"
        env["BLACK_STATE_LOG"] = "1"
        env["RECOMP_FPSLOG"] = "1"
    if args.presenter_pacing == "legacy":
        env["BLACK_LEGACY_PACING"] = "1"
    if args.gamma == "off":
        env["BLACK_GAMMA"] = "off"
    if args.gamma_trace:
        env["BLACK_GAMMA_TRACE"] = "1"
    if args.effect_filter == "off":
        env["BLACK_EFFECT_FILTER"] = "off"
    if args.diagnostics:
        env["BLACK_EFFECT_FILTER_TRACE"] = "1"
    if args.gpu_readback == "on-demand":
        env["RECOMP_NO_FLIP_DOWNLOAD"] = "1"
    if args.capture:
        captures = run_dir / "frames"
        captures.mkdir()
        env["RECOMP_FB_SNAP"] = str(captures) + "/"
        window_frames = run_dir / "window-frames"
        window_frames.mkdir()
        env["RECOMP_FB_OUTSNAP"] = str(window_frames) + "/"
        print(f"Guest frames: {captures}\nPresented frames: {window_frames}", flush=True)
    if args.slot_pictures:
        pictures = run_dir / "slot-pictures"
        pictures.mkdir()
        env["BLACK_SLOT_PICTURES"] = str(pictures)
    if args.black_box:
        boxes = run_dir / "black-box"
        boxes.mkdir()
        env["BLACK_BLACK_BOX_DIR"] = str(boxes)
        print(f"Black Box stream: {boxes}", flush=True)
    if args.replay_input:
        env["BLACK_INPUT_REPLAY"] = str(Path(args.replay_input).expanduser().resolve(strict=True))
    if args.mute:
        env["SDL_AUDIODRIVER"] = "dummy"
    if args.input_script:
        env["RECOMP_INPUT_SCRIPT"] = args.input_script
    if args.audio_capture:
        env["RECOMP_AUDIO_DUMP"] = str(run_dir / "audio.s16le")
        env["BLACK_AUDIO_FLOAT_DUMP"] = str(run_dir / "audio-mix.f32le")
    if args.audio_stages:
        env["BLACK_AUDIO_STAGES"] = "1"
    if args.input_device is not None:
        env["BLACK_INPUT_DEVICE"] = args.input_device
    env["BLACK_DSP_HEADROOM_DB"] = str(args.dsp_headroom)
    if args.test_quit_after:
        env["BLACK_TEST_QUIT_AFTER"] = str(args.test_quit_after)
    if args.test_home_quit:
        env["BLACK_TEST_HOME_QUIT"] = str(args.test_home_quit) + (":stay" if args.test_home_stay else "")
    if args.audio_engine == "dsp":
        env["RECOMP_REAL_DSP"] = "1"
        env["RECOMP_AUDIOLOG"] = "1"
    if args.audio_output == "clip":
        env["BLACK_LEGACY_AUDIO_CLIP"] = "1"
    if args.frame_source == "snapshot":
        env["BLACK_STABLE_FRAMES"] = "1"
    if args.test_input:
        env["RECOMP_INPUT_SCRIPT"] = args.input_script or "600000:a:1"
        env["RECOMP_INPUT_FILE"] = str(run_dir / "input.txt")
        print(f"Test input file: {run_dir / 'input.txt'}", flush=True)
    if args.script_only:
        env["RECOMP_BACKGROUND"] = "1"
    if args.headless:
        env["BLACK_HEADLESS"] = "1"
        env["RECOMP_BACKGROUND"] = "1"
    if args.metal:
        env["DYLD_LIBRARY_PATH"] = os.environ["BLACK_ANGLE_LIB_DIR"]
    log_path = run_dir / "game.log"
    last_log = state / "last-run.log"
    if last_log.is_symlink():
        last_log.unlink()
    elif last_log.exists():
        last_log.rename(run_dir / "previous-run.log")
    last_log.symlink_to(log_path.relative_to(state))
    launch = {"binary_sha256": digest(binary), "game": data, "preset": name,
              "settings_initial_sha256": digest(env["BLACK_SETTINGS_FILE"]) if Path(env["BLACK_SETTINGS_FILE"]).is_file() else None,
              "save_directory": str((run_dir if args.dev_unlock_levels is not None else state) / "data/xboxrecomp"),
              "smoke_seconds": args.smoke_seconds,
              "runtime_environment": {k: v for k, v in env.items()
                                      if k.startswith(("RECOMP_", "BLACK_")) or k in ("SDL_AUDIODRIVER", "SDL_NO_SIGNAL_HANDLERS")}}
    if args.game_mode:
        launch["local_app"] = args.game_mode_bundle
        launch["game_mode"] = {"opt_in": True, "native_fullscreen_requested": not args.windowed,
                               "activation": "Controlled by macOS; opt-in does not prove active Game Mode"}
    if args.metal:
        library_dir = Path(os.environ["BLACK_ANGLE_LIB_DIR"])
        launch["angle_libraries"] = {name: digest(library_dir / name)
                                     for name in ("libEGL.dylib", "libGLESv2.dylib")}
    saves_before = None
    if args.backup_saves:
        print("Backing up the full save directory and verifying SHA-256 before launch...", flush=True)
        saves_before = save_evidence.backup(launch["save_directory"], run_dir)
        launch["save_backup"] = "saves-before.json"
        print(f"Verified save backup: {run_dir / 'saves-before.tar.gz'}", flush=True)
    (run_dir / "launch.json").write_text(json.dumps(launch, indent=2) + "\n")
    print(f"Log: {log_path}\nCmd+Return: fullscreen; Esc: pause/release mouse", flush=True)
    if args.test_unit is not None:
        print(f"Developer start: the next mission begins at streaming unit {args.test_unit}; not a gameplay pass.", flush=True)
    if args.checkpoint_test:
        print("Checkpoint test: the game's next checkpoint is written beside the saves; not a gameplay pass.", flush=True)
    if not args.metal:
        print("Renderer: legacy OpenGL (--opengl), for comparisons only; Metal is the default and much faster in heavy scenes.", flush=True)
    if args.game_clock == "console":
        print("Gameplay: original fixed step, paced at about 30 fps; FMV timing stays original.", flush=True)
    if args.script_only:
        print("SCRIPTED DIAGNOSTICS: physical controllers and mouse gameplay are disabled.", flush=True)
    with log_path.open("w") as log:
        process = subprocess.Popen([str(binary), data["game_dir"]], cwd=state, env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            result = process.wait(timeout=args.smoke_seconds or None)
            (run_dir / "outcome.json").write_text(json.dumps({"timed_stop": False, "exit_code": result}, indent=2) + "\n")
        except subprocess.TimeoutExpired:
            forced = stop_child(process)
            (run_dir / "outcome.json").write_text(json.dumps({"timed_stop": True, "forced_kill": forced,
                                                            "exit_code": process.returncode}, indent=2) + "\n")
            print("Timed smoke stopped by the launcher. Inspect logs/frames; this is not an automatic gameplay PASS.")
            return
        except KeyboardInterrupt:
            forced = stop_child(process)
            (run_dir / "outcome.json").write_text(json.dumps({"interrupted": True, "forced_kill": forced,
                                                            "exit_code": process.returncode}, indent=2) + "\n")
            return
        finally:
            if saves_before is not None:
                changes = save_evidence.record_after(launch["save_directory"], run_dir, saves_before)
                print(f"Save file changes: {json.dumps(changes)}; details: {run_dir / 'saves-after.json'}", flush=True)
    if result:
        raise ValueError(f"BLACK exited with {result}; see {log_path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name, function in (("setup", setup), ("doctor", doctor), ("generate", generate), ("build", build), ("check", check), ("run", play)):
        sub = commands.add_parser(name)
        sub.set_defaults(function=function)
        if name == "generate":
            sub.add_argument("source", help="Your own full game folder or Xbox ISO/XISO")
        if name in ("build", "check"):
            sub.add_argument("--jobs", type=int, choices=range(1, 33), default=8)
        if name == "build":
            sub.add_argument("--real-dsp", action="store_true", help="Also build the pinned experimental DSP56300 JIT (requires Rust)")
        if name == "check":
            sub.add_argument("--graphics", action="store_true", help="Also test a real GPU context (requires a desktop session)")
        if name in ("build", "run"):
            add_renderer(sub)
        if name == "run":
            sub.add_argument("--backup-saves", action="store_true", help="Verify a compressed backup of the entire save root before launch; record file hashes/changes after exit (no restore or progress edits)")
            sub.add_argument("--test-state-dir", help="Persistent save/settings sandbox below out/macos/validation for repeated bounded runs; requires --backup-saves, no developer unlocks; never imports data automatically")
            sub.add_argument("--scale", type=int, choices=range(1, 7), help="Override saved render scale (default 2)")
            sub.add_argument("--fov", type=int, choices=range(45, 141), help="Override saved field of view (default 70)")
            sub.add_argument("--aa", choices=tuple(AA_MODES), help="Override saved anti-aliasing: off, fxaa or smaa (default)")
            sub.add_argument("--dev-unlock-levels", type=int, nargs="?", const=7, choices=range(8), metavar="N",
                             help="Developer qualification: open missions 0..N (default all) with N preselected; saves go to the run's own throwaway directory")
            sub.add_argument("--upscale", choices=("off", "metalfx"), help="Override saved upscaler: metalfx (default, spatial to the window's pixel size) or off (bilinear)")
            sub.add_argument("--af", type=int, choices=ANISO_LEVELS, help="Override saved anisotropic filtering; 0 keeps the game's own level")
            sub.add_argument("--aspect", choices=("4:3", "16:9"), default="16:9", help="Presentation shape; not an ultrawide camera/HUD patch")
            sub.add_argument("--windowed", action="store_true")
            sub.add_argument("--game-mode", action="store_true", help="Use local signed BLACK.app with Game Mode opt-in; macOS activates it in native full screen")
            sub.add_argument("--open-settings", nargs="?", const="1", default=None, metavar="PAGE[/ROW]",
                             help="Open the recomp settings at launch; optionally on a page by its English name (e.g. MODS/12) for screenshots")
            sub.add_argument("--settings-file", help="Use an explicit settings file (separate profiles or isolated developer checks)")
            sub.add_argument("--texture-dir", help="Folder holding replacement/ with your own HD texture pack (default: ~/Library/Application Support/BLACK/Textures)")
            sub.add_argument("--texture-audit", action="store_true", help="Record unique texture keys and map matches in this run's texture-audit.csv; no pixels")
            sub.add_argument("--texture-dump", action="store_true", help="Dump original top-level textures to this run's private folder (DDS or PNG)")
            sub.add_argument("--subtitle-dir", help="Experimental: use SRT files from this folder for cutscenes")
            sub.add_argument("--test-game-view", help="Use a validated separate game view for a bounded isolated developer run")
            sub.add_argument("--headless", action="store_true", help="Bounded muted GPU validation without opening the game window")
            sub.add_argument("--pad-deadzone", type=int, choices=range(0, 16001), help="Override both saved stick deadzones until edited in Input; 0 keeps the original 20-percent response, nonzero is an exact SDL-count threshold")
            sub.add_argument("--input-device", choices=("auto", "gamepad", "keyboard"), default=None, help="Exclusive gameplay input; auto ignores mouse motion while the pad owns control, a deliberate key/button switches back after the pad settles")
            sub.add_argument("--game-clock", choices=("console", "original", "realtime"), default="console", help="Console: original world step at its intended rate; original: uncapped legacy; realtime: experimental variable-FPS correction")
            sub.add_argument("--audio-routing", choices=("black", "legacy"), default="black", help="BLACK speaker-bin downmix; legacy sums all bins by parity for A/B checks")
            sub.add_argument("--audio-engine", choices=("dry", "dsp"), default="dry", help="Experimental real GP/EP needs build --real-dsp; dry is the existing speaker downmix")
            sub.add_argument("--audio-output", choices=("limiter", "clip"), default="limiter", help="Lookahead output protection; clip reproduces the old hard saturation for comparison")
            sub.add_argument("--audio-pacing", choices=("queue", "legacy"), default="queue", help="Queue-driven audio pacing adapted from current xboxrecomp")
            sub.add_argument("--voice-gain", choices=("ramp", "legacy"), default="ramp", help="Interpolate changing voice gains over each 32-sample block, as in xboxrecomp v0.16")
            sub.add_argument("--world-wait", choices=("precise", "legacy"), default="precise", help="macOS deadline sleep for console pacing; legacy reproduces nanosleep coalescing")
            sub.add_argument("--skip-movies", action="store_true", help="Developer bypass: complete all non-background films, including story scenes; default off")
            sub.add_argument("--skip-intros", action="store_true", help="Complete startup logos/intro only; leave story scenes and native progress-based skip rules intact")
            sub.add_argument("--state-log", action="store_true", help="Sample guest camera/world state for input diagnostics")
            sub.add_argument("--diagnostics", action="store_true")
            sub.add_argument("--save-trace", action="store_true", help="Log save-folder enumeration and file allocation calls")
            sub.add_argument("--test-unit", type=int, choices=range(1, 128), help="Developer start: begin a mission at this streaming unit, to reach a checkpoint without replaying earlier parts")
            sub.add_argument("--checkpoint-test", action="store_true", help="Developer test: write the game's next checkpoint under the run data; not a gameplay pass")
            sub.add_argument("--restore-window", action="store_true", help="Window or full screen as last left, as the app does (overrides --windowed)")
            sub.add_argument("--frame-times", action="store_true", help="Log game FPS and longest frame every two seconds without full diagnostics")
            sub.add_argument("--profile-frames", action="store_true", help="Buffered per-frame CPU/GPU-readback/presenter timing, no images")
            sub.add_argument("--gamma", choices=("game", "off"), default="game", help="Apply the game's own brightness/gamma ramp at presentation (default), or keep identity for A/B")
            sub.add_argument("--effect-filter", choices=("box", "off"), default="box", help="Average BLACK's small glow/blur targets over one Xbox texel when the render scale is above 1 (default), or keep the stock copy for A/B")
            sub.add_argument("--gamma-trace", action="store_true", help="Log each new game gamma ramp")
            sub.add_argument("--presenter-pacing", choices=("steady", "legacy"), default="steady", help="Host presentation pacing; legacy reproduces the upstream extra sleep for comparison")
            sub.add_argument("--gpu-readback", choices=("auto", "always", "on-demand"), default="auto", help="Surface downloads to guest RAM: auto (on-demand at scale >= 2, always at 1x), always (every flip, the old behaviour) or on-demand")
            sub.add_argument("--frame-source", choices=("snapshot", "legacy"), default="snapshot", help="Completed GPU snapshots also at native resolution; legacy selects guest RAM buffers for comparison")
            sub.add_argument("--capture", action="store_true", help="Capture framebuffer images locally")
            sub.add_argument("--mute", action="store_true", help="Use SDL's dummy audio device for a silent smoke")
            sub.add_argument("--slot-pictures", action="store_true", help="Also write each picture kept for the Saves page to this run's slot-pictures/")
            sub.add_argument("--black-box", action="store_true", help="Stream every guest-visible pad poll of this run to an input CSV inside black-box/")
            sub.add_argument("--replay-input", metavar="CSV", help="Feed a saved Black Box CSV to the game instead of the pad; the real game still runs")
            sub.add_argument("--input-script", help="Runtime test harness: ms:button[:hold_ms], comma separated; times from boot")
            sub.add_argument("--test-input", action="store_true", help="Enable the runtime's local input.txt test harness")
            sub.add_argument("--script-only", action="store_true", help="Ignore physical pads/window input during a controlled scripted validation run")
            sub.add_argument("--audio-capture", action="store_true", help="Record accepted game PCM buffers locally (not a tone)")
            sub.add_argument("--audio-stages", action="store_true", help="Measure decoded voice/filter clipping and per-bin DSP input peaks")
            sub.add_argument("--dsp-headroom", type=int, choices=(0, 6, 12, 18), default=0, help="Experimental input headroom before DSP 24-bit conversion, compensated at its output; 0 preserves original levels")
            sub.add_argument("--test-home-quit", type=int, choices=range(1, 601), help="Open the Home quit dialog after N seconds and choose Quit to Main Menu (Quit Game outside a mission) a second later")
            sub.add_argument("--test-home-stay", action="store_true", help="With --test-home-quit: only open the dialog, choose nothing")
            sub.add_argument("--test-quit-after", type=int, choices=range(1, 601), help="Post a local Cmd+Q event for a bounded exit integration check")
            sub.add_argument("--smoke-seconds", type=int, choices=range(1, 601), help="Stop after this many seconds; not a gameplay pass criterion")
    args = parser.parse_args()
    try:
        args.function(args)
    except (ValueError, OSError, subprocess.CalledProcessError, tarfile.TarError) as error:
        parser.exit(1, f"BLACK: {error}\n")


if __name__ == "__main__":
    main()
