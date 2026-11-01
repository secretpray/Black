"""Local app identity for Game Mode; validated launcher still owns game data and runs."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

INFO = Path(__file__).with_name("BLACK-Info.plist")
ICON = Path(__file__).resolve().parents[2] / "resources/macos/BLACK.icns"


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def prepare(binary, *, sign=True):
    binary = Path(binary).resolve()
    bundle = binary.parent / "BLACK.app"
    executable = bundle / "Contents/MacOS/BLACK"
    manifest = binary.parent / "BLACK.app.manifest.json"
    source_sha, info_sha, icon_sha = digest(binary), digest(INFO), digest(ICON)
    try:
        prior = json.loads(manifest.read_text())
        if (prior["source_sha256"] == source_sha and prior["info_sha256"] == info_sha
                and prior.get("icon_sha256") == icon_sha and digest(bundle / "Contents/Resources/BLACK.icns") == icon_sha
                and prior["ad_hoc_signed"] == sign and digest(executable) == prior["executable_sha256"]
                and digest(bundle / "Contents/Info.plist") == info_sha):
            return executable, prior
    except (OSError, KeyError, ValueError):
        pass
    with tempfile.TemporaryDirectory(prefix=".black-app-", dir=binary.parent) as temp:
        staged = Path(temp) / "BLACK.app"
        target = staged / "Contents/MacOS/BLACK"
        target.parent.mkdir(parents=True)
        # Signing changes the copy only, never the source build or a hard link.
        shutil.copy2(binary, target)
        target.chmod(0o755)
        shutil.copyfile(INFO, staged / "Contents/Info.plist")
        (staged / "Contents/Resources").mkdir()
        shutil.copyfile(ICON, staged / "Contents/Resources/BLACK.icns")
        if sign:
            subprocess.run(["/usr/bin/codesign", "--force", "--sign", "-", str(staged)], check=True)
            subprocess.run(["/usr/bin/codesign", "--verify", "--strict", str(staged)], check=True)
        if digest(binary) != source_sha:
            raise RuntimeError("Source executable changed while preparing the local app")
        record = {"source_sha256": source_sha, "info_sha256": info_sha, "icon_sha256": icon_sha,
                  "executable_sha256": digest(target), "ad_hoc_signed": sign,
                  "game_mode_opt_in": True, "category": "public.app-category.action-games",
                  "scope": "Local build; libraries and game data supplied by the validated launcher"}
        previous = Path(temp) / "previous.app"
        if bundle.exists():
            bundle.rename(previous)
        try:
            staged.rename(bundle)
        except OSError:
            if previous.exists():
                previous.rename(bundle)
            raise
        pending = manifest.with_suffix(".tmp")
        pending.write_text(json.dumps(record, indent=2) + "\n")
        os.replace(pending, manifest)
    return executable, record
