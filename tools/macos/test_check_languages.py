"""Language bank/font check on synthetic banks and fonts; no game files."""
import struct
import unittest

import check_languages
import russian_font

TEX = 0x220 + 40 * 16


def bank(entries):
    """String bank bytes for {id: text}."""
    count = len(entries)
    body = bytearray()
    offsets = []
    for key, text in entries.items():
        offsets.append(16 + 4 * count + len(body))
        body += struct.pack("<I", key) + text.encode("utf-8") + b"\0"
        body += b"\0" * (-len(body) % 4)
    return struct.pack("<IIII", 0, 0, count, 16) + struct.pack(f"<{count}I", *offsets) + bytes(body)


def font(chars):
    """'4.1v' font bytes with a glyph record for each character."""
    b = bytearray(TEX + 128 + 16)
    b[:4] = b"4.1v"
    struct.pack_into("<I", b, 4, TEX)
    struct.pack_into("<I", b, TEX + 12, russian_font.DXT3 << 8 | 2 << 20 | 2 << 24)
    for n, ch in enumerate(chars):
        struct.pack_into("<7fH", b, 0x220 + 40 * n, 0, 0, 0.5, 0.5, 0, 0, 0.5, ord(ch))
    return bytes(b)


ENGLISH = {1: "PRESS  TO FIRE", 2: "MENU", check_languages.CHARSET_TEST: "ÄÖÜあ"}


class CheckLanguagesTest(unittest.TestCase):
    def test_bank_round_trip(self):
        self.assertEqual(check_languages.strings(bank(ENGLISH)), ENGLISH)

    def test_accents_with_glyphs_pass_and_english_symbols_are_baseline(self):
        fonts = {"Big.bin": font("PRESTOFIMNUÜ"), "Small.bin": font("PRESTOFIMNUÜ")}
        report = check_languages.check({"UK": bank(ENGLISH), "Ge": bank({**ENGLISH, 2: "MENÜ"})}, fonts)
        self.assertEqual(report["UK"], (3, "", []))
        self.assertEqual(report["Ge"], (3, "Ü", []))

    def test_missing_glyph_and_ids_are_reported(self):
        fonts = {"Big.bin": font("PRESTOFIMNUÜ"), "Small.bin": font("PRESTOFIMNU")}
        report = check_languages.check({"UK": bank(ENGLISH), "Ge": bank({1: "MENÜ", 3: "X"})}, fonts)
        problems = report["Ge"][2]
        self.assertIn("2 IDs missing, 1 extra", problems)
        self.assertEqual([p for p in problems if "glyph" in p], ["Big.bin has no glyph for 'X' U+0058",
                                                                 "Small.bin has no glyph for 'X' U+0058 'Ü' U+00DC"])

    def test_malformed_banks_are_rejected(self):
        good = bank(ENGLISH)
        for bad in (good[:12], good[:8] + struct.pack("<II", 3, 20) + good[16:], good[:-4]):
            with self.assertRaises(ValueError):
                check_languages.strings(bad)
        with self.assertRaises(UnicodeDecodeError):
            check_languages.strings(bank({1: "A"}).replace(b"A", b"\xff"))


if __name__ == "__main__":
    unittest.main()
