#!/usr/bin/env python3
"""Write Finder layout directly, with the same installation flow as Burnout Revenge."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import dmgbuild

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Output already exists")
    with tempfile.TemporaryDirectory(prefix="black-dmg-") as tmp:
        tmp = Path(tmp)
        for scale in (1, 2):
            subprocess.run(["sips", "-z", str(440 * scale), str(660 * scale),
                            str(ROOT / "resources/macos/DMGBackground.png"), "--out", str(tmp / f"bg{scale}.png")], check=True, stdout=subprocess.DEVNULL)
        subprocess.run(["tiffutil", "-cathidpicheck", str(tmp / "bg1.png"), str(tmp / "bg2.png"), "-out", str(tmp / "background.tiff")], check=True)
        settings = {
            "format": "UDZO", "compression_level": 9, "filesystem": "HFS+",
            "files": [(str(args.app.resolve()), "BLACK.app")], "symlinks": {"Applications": "/Applications"},
            "icon": str(ROOT / "resources/macos/BLACK.icns"), "background": str(tmp / "background.tiff"),
            "window_rect": ((200, 140), (660, 508)), "default_view": "icon-view",
            "show_status_bar": False, "show_tab_view": False, "show_toolbar": False,
            "show_pathbar": False, "show_sidebar": False, "show_icon_preview": False,
            "icon_size": 128, "text_size": 13, "arrange_by": None, "label_pos": "bottom",
            "icon_locations": {"BLACK.app": (172, 210), "Applications": (487, 210)},
        }
        dmgbuild.build_dmg(str(args.output.resolve()), "BLACK", settings=settings)


if __name__ == "__main__":
    main()
