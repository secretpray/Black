#!/usr/bin/env python3
"""Build all macOS ICNS sizes from the full-bleed square app artwork."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = root / "resources/macos/AppIcon.png"
with tempfile.TemporaryDirectory(prefix="black-icon-") as tmp:
    iconset = Path(tmp) / "BLACK.iconset"
    iconset.mkdir()
    for size in (16, 32, 128, 256, 512):
        for scale in (1, 2):
            name = f"icon_{size}x{size}" + ("@2x" if scale == 2 else "") + ".png"
            subprocess.run(["sips", "-z", str(size * scale), str(size * scale), str(source),
                            "--out", str(iconset / name)], check=True, stdout=subprocess.DEVNULL)
    subprocess.run(["iconutil", "-c", "icns", str(iconset), "-o", str(root / "resources/macos/BLACK.icns")], check=True)
