#!/usr/bin/env python3
"""Ship exact runtime sources and Homebrew library sources beside a binary release."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[2]


def digest(p):
    with p.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def archive_git(repo, output):
    # git archive cannot include build caches, private logs or ignored game data.
    with output.open("wb") as out:
        subprocess.run(["git", "-C", str(repo), "archive", "HEAD"], stdout=out, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--version", default="0.2.0")
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Output already exists")
    cache = ROOT / "out/macos/library-sources"
    cache.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="black-sources-") as tmp:
        stage = Path(tmp) / "BLACK-library-sources"
        stage.mkdir()
        inventory = []
        for recipe in sorted((args.app / "Contents/Resources/Licenses").glob("*/formula/*.rb")):
            text = recipe.read_text()
            url = re.search(r'^  url "([^"]+)"', text, re.M).group(1)
            sha = re.search(r'^  sha256 "([a-f0-9]+)"', text, re.M).group(1)
            if "ftpmirror.gnu.org" in url:
                url = url.replace("ftpmirror.gnu.org", "ftp.gnu.org")
            name = recipe.stem + "-" + url.rsplit("/", 1)[-1]
            source = cache / name
            if not source.is_file() or digest(source) != sha:
                pending = source.with_suffix(source.suffix + ".tmp")
                print("Downloading", recipe.stem, flush=True)
                with urllib.request.urlopen(url, timeout=120) as response, pending.open("wb") as out:
                    shutil.copyfileobj(response, out)
                if digest(pending) != sha:
                    raise ValueError(f"Source checksum mismatch: {name}")
                pending.replace(source)
            shutil.copy2(source, stage / name)
            shutil.copy2(recipe, stage / recipe.name)
            inventory.append({"component": recipe.stem, "url": url, "sha256": sha, "source": name, "recipe": recipe.name})
        for path, name in [(ROOT / "ports/macos/runtime", "runtime"), (ROOT / "ports/macos/game", "game-host"),
                           (ROOT / "ports/macos/runtime/third_party/dsp56300", "dsp56300")]:
            archive_git(path, stage / f"{name}.tar")
            revision = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
            inventory.append({"component": name, "revision": revision, "source": f"{name}.tar"})
        # Include all local source overlays/configuration, excluding generated C.
        for name in ("cmake", "tools/macos", "ports/macos/overrides", "ports/macos/licenses"):
            shutil.copytree(ROOT / name, stage / name, ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
        for name in ("CMakeLists.txt", "CMakePresets.json", "THIRD_PARTY_NOTICES.md"):
            shutil.copy2(ROOT / name, stage / name)
        shutil.copy2(ROOT / "out/macos/angle/provenance.json", stage / "angle-provenance.json")
        # ANGLE is BSD; retain its exact pin and official recursive build procedure.
        shutil.copy2(ROOT / ".research/angle-root/angle/LICENSE", stage / "ANGLE-LICENSE")
        (stage / "manifest.json").write_text(json.dumps(inventory, indent=2) + "\n")
        (stage / "README.txt").write_text(
            f"BLACK {args.version} library source and relinking materials\n\n"
            "Exact runtime/game host/DSP source archives, local overlays, and shipped Homebrew\n"
            "library source archives with the original build recipes and verified SHA-256.\n"
            "No ISO, XBE, game assets, generated game C, saves or local session logs are included.\n\n"
            f"Clone https://github.com/secretpray/Black at tag v{args.version} with submodules.\n"
            "Follow docs/BUILD.md and generate native code from your own supported disc.\n"
            "To modify static runtime libraries, edit the included source, pin that revision\n"
            "explicitly in tools/macos/black.py, clear the build directory and rebuild.\n"
            "Dynamically linked libraries live in BLACK.app/Contents/Frameworks: rebuild\n"
            "from the matching archive/recipe, replace a library with a compatible arm64\n"
            "version, keep its @rpath identity and loader paths, then ad-hoc sign the library\n"
            "and the app with codesign --force --sign -. Reverse engineering needed to debug\n"
            "modifications to the LGPL components is permitted.\n"
            "ANGLE's exact revision and build arguments are in angle-provenance.json;\n"
            "tools/macos/build_angle.py obtains official source and dependency pins.\n"
            "All upstream licenses and copyright notices continue to apply.\n")
        with tarfile.open(args.output, "w:xz") as archive:
            archive.add(stage, arcname=stage.name)
    print(args.output)


if __name__ == "__main__":
    main()
