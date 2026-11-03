#!/usr/bin/env python3
"""Check that each installed Main*.bin string bank can be drawn by the game fonts.

Every bank must have exactly the string IDs of the English bank, decode as UTF-8,
and use no character beyond English that Big.bin or Small.bin lacks a glyph
record for. English is the baseline: its own button icons (private-use codes),
box/arrow symbols and the charset test string are drawn by other paths or never
shown, so they are not reported again for each language. This only searches the
files; whether text fits on screen is checked in the running game.

    python3 tools/macos/check_languages.py out/macos/normalized-game
"""
import argparse
from pathlib import Path
import struct
import sys

from russian_font import Font

FONTS = ("Big.bin", "Small.bin")
BASELINE = "UK"
# A string listing the characters of every language, including Japanese and Korean.
CHARSET_TEST = 0xB7E3170E


def strings(data):
    """{id: text} of a string bank: u32 count at +8, u32 table offset at +12, and
    per entry a u32 ID followed by NUL-terminated UTF-8."""
    if len(data) < 16:
        raise ValueError("String bank header is truncated")
    count, table = struct.unpack_from("<II", data, 8)
    if table != 16 or count > 100000 or table + 4 * count > len(data):
        raise ValueError("Invalid string bank table")
    offsets = struct.unpack_from(f"<{count}I", data, table)
    bank = {}
    for i, start in enumerate(offsets):
        end = offsets[i + 1] if i + 1 < count else len(data)
        if start < table + 4 * count or start + 5 > end or end > len(data):
            raise ValueError("Invalid string bank entry")
        key = struct.unpack_from("<I", data, start)[0]
        text = data[start + 4:end].split(b"\0", 1)
        if key in bank or len(text) < 2:
            raise ValueError(f"Duplicate or unterminated string {key:#010x}")
        bank[key] = text[0].decode("utf-8")
    return bank


def check(banks, fonts):
    """Problems per language, for banks {suffix: bytes} with BASELINE among them
    and fonts {name: bytes}."""
    glyphs = {name: set(Font(data).recs) for name, data in fonts.items()}
    parsed = {suffix: strings(data) for suffix, data in banks.items()}
    base = parsed[BASELINE]
    known = {ch for key, text in base.items() if key != CHARSET_TEST for ch in text}
    report = {}
    for suffix, bank in parsed.items():
        problems = []
        if set(bank) != set(base):
            problems.append(f"{len(set(base) - set(bank))} IDs missing, {len(set(bank) - set(base))} extra")
        used = {ch for key, text in bank.items() if key != CHARSET_TEST for ch in text}
        for name, codes in glyphs.items():
            lacking = sorted(ch for ch in used - known if ord(ch) not in codes)
            if lacking:
                problems.append(f"{name} has no glyph for " + " ".join(f"{ch!r} U+{ord(ch):04X}" for ch in lacking))
        report[suffix] = (len(bank), "".join(sorted(used - known)), problems)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("game", type=Path, help="game folder with language/strings and language/fonts")
    args = parser.parse_args()
    folder = args.game / "language"
    banks = {p.stem[4:]: p.read_bytes() for p in sorted((folder / "strings").glob("Main*.bin"))}
    if BASELINE not in banks:
        raise SystemExit(f"Main{BASELINE}.bin is missing")
    report = check(banks, {name: (folder / "fonts" / name).read_bytes() for name in FONTS})
    failed = False
    for suffix, (count, extra, problems) in report.items():
        shown = " ".join(ch if ch.isprintable() and not ch.isspace() else f"U+{ord(ch):04X}" for ch in extra)
        print(f"Main{suffix}.bin: {count} strings; beyond English: {shown or '-'}")
        for problem in problems:
            print(f"  FAIL {problem}")
        failed |= bool(problems)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
