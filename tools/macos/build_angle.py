#!/usr/bin/env python3
"""Build the pinned official ANGLE Metal libraries; no libraries from other apps."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
ANGLE_REVISION = "56e0454d0d34942c7936230830aeccccbb994a2f"
DEPOT_REVISION = "37e67d87efb841c1633c9943cdc334ecb523b921"
ARGS = ('target_cpu="arm64" is_debug=false is_component_build=false symbol_level=0 '
        'angle_enable_metal=true angle_enable_gl=false angle_enable_vulkan=false '
        'angle_enable_swiftshader=false angle_enable_wgpu=false angle_build_tests=false '
        'build_angle_deqp_tests=false clang_use_chrome_plugins=false '
        'use_remoteexec=false use_siso=false')


def run(command, **kwargs):
    print("+", " ".join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), check=True, **kwargs)


def checkout(url, path, revision):
    if not path.exists():
        run(["git", "clone", "--depth", "1", url, path])
    head = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
    if head != revision:
        dirty = subprocess.check_output(["git", "-C", str(path), "status", "--porcelain", "--untracked-files=no"], text=True)
        if dirty:
            raise ValueError(f"Tracked edits in {path}; move them aside before changing revisions")
        run(["git", "-C", path, "fetch", "--depth", "1", "origin", revision])
        run(["git", "-C", path, "checkout", "--detach", revision])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, choices=range(1, 33), default=8)
    parser.add_argument("--skip-sync", action="store_true", help="Reuse dependencies already synced for these pins")
    args = parser.parse_args()
    ninja = shutil.which("ninja")
    if not ninja:
        raise ValueError("Ninja is missing; install with brew install ninja")
    depot = ROOT / ".research/depot_tools"
    source_root = ROOT / ".research/angle-root"
    source_root.mkdir(parents=True, exist_ok=True)
    source = source_root / "angle"
    build = source / "out/Metal"
    dest = ROOT / "out/macos/angle/lib"
    checkout("https://chromium.googlesource.com/chromium/tools/depot_tools.git", depot, DEPOT_REVISION)
    checkout("https://chromium.googlesource.com/angle/angle.git", source, ANGLE_REVISION)
    metal = subprocess.run(["xcrun", "metal", "--version"], capture_output=True, text=True)
    if metal.returncode:
        raise ValueError("Xcode Metal compiler unavailable. Install with: xcodebuild -downloadComponent MetalToolchain")
    config = {"name": "angle", "url": "https://chromium.googlesource.com/angle/angle.git", "managed": False,
              "custom_deps": {}, "custom_vars": {"checkout_angle_cl_deps": False,
              "checkout_angle_dawn_deps": False, "checkout_angle_restricted_traces": False,
              "checkout_extra_traces": False, "checkout_angle_internal": False}}
    # .gclient is a Python literal, not JSON (it requires False).
    (source_root / ".gclient").write_text("solutions = " + repr([config]) + "\n")
    env = {**os.environ, "PATH": str(depot) + os.pathsep + os.environ["PATH"], "DEPOT_TOOLS_UPDATE": "0"}
    if not args.skip_sync:
        run([depot / "gclient", "sync", "--shallow", "--no-history", "--nohooks"], cwd=source_root, env=env)
        run([depot / "gclient", "runhooks"], cwd=source_root, env=env)
    run([source / "buildtools/mac/gn", "gen", build, "--root=" + str(source), "--args=" + ARGS], env=env)
    run([ninja, "-C", build, "-j", args.jobs, "libEGL", "libGLESv2"], env=env)
    dest.mkdir(parents=True, exist_ok=True)
    hashes = {}
    for name in ("libEGL.dylib", "libGLESv2.dylib"):
        path = build / name
        archs = subprocess.check_output(["lipo", "-archs", str(path)], text=True).strip()
        if archs != "arm64":
            raise ValueError(f"Expected arm64 {name}, got {archs}")
        shutil.copy2(path, dest / name)
        run(["install_name_tool", "-id", "@rpath/" + name, dest / name])
        run(["codesign", "--force", "--sign", "-", dest / name])
        hashes[name] = hashlib.sha256((dest / name).read_bytes()).hexdigest()
    shutil.copy2(source / "LICENSE", dest.parent / "ANGLE-LICENSE")
    record = {"repository": "https://chromium.googlesource.com/angle/angle.git",
              "angle_revision": ANGLE_REVISION, "depot_tools_revision": DEPOT_REVISION,
              "gn_args": ARGS, "compiler": metal.stdout.strip(), "libraries": hashes}
    (dest.parent / "provenance.json").write_text(json.dumps(record, indent=2) + "\n")
    print(f"Built local Metal libraries: {dest}\nBuild BLACK with: python3 tools/macos/black.py build --metal")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(f"ANGLE: {error}")
