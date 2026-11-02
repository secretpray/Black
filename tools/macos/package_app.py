#!/usr/bin/env python3
"""Assemble a relocatable Apple Silicon app; never copy game data or user state."""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def run(*args):
    return subprocess.check_output(list(map(str, args)), text=True, stderr=subprocess.STDOUT)


def dependencies(path):
    return [line.strip().split(" (compatibility", 1)[0] for line in run("otool", "-L", path).splitlines()[1:]]


def rpaths(path):
    return re.findall(r"cmd LC_RPATH\n\s*cmdsize \d+\n\s*path (.*?) \(offset", run("otool", "-l", path))


def system(name):
    return name.startswith(("/System/Library/", "/usr/lib/"))


def build(binary, output, version):
    binary, output = binary.resolve(), output.resolve()
    if output.exists():
        raise ValueError(f"Output already exists: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".package-", dir=output.parent) as tmp:
        app = Path(tmp) / "BLACK.app"
        mac, lib, resources = (app / "Contents" / part for part in ("MacOS", "Frameworks", "Resources"))
        for path in (mac, lib, resources):
            path.mkdir(parents=True)
        executable = mac / "BLACK"
        shutil.copy2(binary, executable)
        # Remove DWARF/local build paths; preserve exported runtime entry points.
        run("strip", "-S", executable)
        shutil.copy2(ROOT / "resources/macos/BLACK.icns", resources)
        shutil.copy2(ROOT / "src/common/europe.patch", resources)
        # portingpete's map (MIT, notices below); the HD pack itself is never shipped.
        shutil.copy2(ROOT / "resources/textures/black-hd-reimagined.map", resources)
        shutil.copy2(ROOT / "resources/textures/black-hd-reimagined-extra.map", resources)
        sources, pending, mapped = {}, [binary], {}
        search = [ROOT / "out/macos/angle/lib", Path("/opt/homebrew/lib")]
        while pending:
            owner = pending.pop()
            for dep in dependencies(owner):
                if system(dep):
                    continue
                name = Path(dep).name
                if dep.startswith("/"):
                    source = Path(dep).resolve()
                else:
                    candidates = [owner.parent / name] + [p / name for p in search]
                    for rp in rpaths(owner):
                        rp = rp.replace("@loader_path", str(owner.parent)).replace("@executable_path", str(binary.parent))
                        candidates.append(Path(rp) / name)
                    source = next((p.resolve() for p in candidates if p.is_file()), None)
                if source is None or not source.is_file():
                    raise ValueError(f"Unresolved dependency: {name}")
                if name in sources and sources[name] != source:
                    raise ValueError(f"Conflicting dependency name: {name}")
                mapped[dep] = name
                if name not in sources:
                    sources[name] = source
                    shutil.copy2(source, lib / name)
                    pending.append(source)
        inventory = []
        minimum = (15, 0)
        for target, original in [(executable, binary)] + [(lib / n, s) for n, s in sources.items()]:
            version_match = re.search(r"\bminos (\d+(?:\.\d+)+)", run("otool", "-l", target))
            if version_match:
                minimum = max(minimum, tuple(map(int, version_match.group(1).split("."))))
            for dep in dependencies(target):
                if not system(dep):
                    run("install_name_tool", "-change", dep, "@rpath/" + Path(dep).name, target)
            for path in rpaths(target):
                run("install_name_tool", "-delete_rpath", path, target)
            run("install_name_tool", "-add_rpath", "@executable_path/../Frameworks", target)
            if target != executable:
                run("install_name_tool", "-id", "@rpath/" + target.name, target)
            if "arm64" not in run("lipo", "-archs", target):
                raise ValueError(f"Not an Apple Silicon binary: {target.name}")
            inventory.append({"file": str(target.relative_to(app)), "source_sha256": hashlib.sha256(original.read_bytes()).hexdigest()})
        info = plistlib.loads((ROOT / "tools/macos/BLACK-Info.plist").read_bytes())
        info.update(CFBundleVersion=version, CFBundleShortVersionString=version,
                    LSMinimumSystemVersion=".".join(map(str, minimum)))
        (app / "Contents/Info.plist").write_bytes(plistlib.dumps(info))
        notices = resources / "Licenses"
        notices.mkdir()
        shutil.copy2(ROOT / "THIRD_PARTY_NOTICES.md", notices)
        for folder, names in [(ROOT, ["LICENSE"]), (ROOT / "ports/macos/runtime", ["LICENSE", "NOTICE", "LICENSES"]),
                              (ROOT / "ports/macos/runtime/third_party/xemu_nv2a", ["COPYING", "COPYING.LIB"]),
                              (ROOT / "ports/macos/runtime/third_party/dsp56300", ["LICENSE"]),
                              (ROOT / "ports/macos/game", ["LICENSE"])]:
            for name in names:
                source = folder / name
                if source.exists():
                    dest = notices / (folder.name + "-" + name)
                    if source.is_dir():
                        shutil.copytree(source, dest)
                    else:
                        shutil.copy2(source, dest)
        # Keep each shipped Homebrew formula and its complete license text.
        formulas = {}
        for source in sources.values():
            parts = source.parts
            if "Cellar" not in parts:
                continue
            index = parts.index("Cellar")
            prefix = Path(*parts[:index + 3])
            formula = parts[index + 1]
            formulas[formula] = prefix
        for formula, prefix in formulas.items():
            dest = notices / formula
            dest.mkdir()
            shutil.copytree(prefix / ".brew", dest / "formula")
            candidates = [p for p in prefix.rglob("*") if p.is_file() and
                          any(t in p.name.upper() for t in ("LICENSE", "COPYING", "COPYRIGHT"))]
            for i, source in enumerate(candidates):
                shutil.copy2(source, dest / f"{i}-{source.name}")
        angle = ROOT / ".research/angle-root/angle"
        if (angle / "LICENSE").is_file():
            shutil.copy2(angle / "LICENSE", notices / "ANGLE-LICENSE")
        for source in angle.rglob("*"):
            if source.is_file() and source.name.upper().startswith(("LICENSE", "LICENCE", "COPYING")) and ".git" not in source.parts:
                dest = notices / "ANGLE-third-party" / source.relative_to(angle)
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, dest)
        if (angle / "out/Metal/gen/angle_license.txt").is_file():
            shutil.copy2(angle / "out/Metal/gen/angle_license.txt", notices / "ANGLE-third-party.txt")
        for name in ("README.md", "CHANGELOG.md"):
            shutil.copy2(ROOT / name, resources / name)
        for target in lib.iterdir():
            run("codesign", "--force", "--sign", "-", target)
        # SDL2-compat opens this unversioned loader-relative name in its
        # constructor; it is not represented by an LC_LOAD_DYLIB command.
        (lib / "libSDL3.dylib").symlink_to("libSDL3.0.dylib")
        manifest = {"version": version, "architecture": "arm64", "minimum_macos": info["LSMinimumSystemVersion"],
                    "bundled_libraries": sorted(sources), "binary_inputs": inventory,
                    "game_data_included": False, "signing": "ad-hoc; not notarized"}
        (resources / "bundle-info.json").write_text(json.dumps(manifest, indent=2) + "\n")
        run("codesign", "--force", "--sign", "-", app)
        run("codesign", "--verify", "--deep", "--strict", app)
        # Validate every load command after relocation, including transitive deps.
        for target in [executable] + list(lib.iterdir()):
            for dep in dependencies(target):
                if not system(dep) and not (dep.startswith("@rpath/") and (lib / Path(dep).name).is_file()):
                    raise ValueError(f"External dependency remains: {target.name}: {dep}")
            if any("/opt/homebrew" in p or str(ROOT) in p for p in rpaths(target)):
                raise ValueError("Local search path remains")
        app.rename(output)
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--version", default="0.2.0")
    args = parser.parse_args()
    build(args.binary, args.output, args.version)
