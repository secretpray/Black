"""Russian font rebuild on a synthetic '4.1v' font; no game or translation files."""
import struct
import unittest

import russian_font

W = H = 128
TEX = 0x220 + 40 * 64


def box(w, h, rows):
    """Bitmap from rows of '#' (15) and '.' (0), checked against its size."""
    bitmap = [[15 if ch == "#" else 0 for ch in row] for row in rows]
    assert len(bitmap) == h and all(len(row) == w for row in bitmap)
    return bitmap


STEM = ["##......##"] * 12
GLYPHS = {
    "N": box(10, 12, STEM),
    "C": box(10, 12, ["##########"] + ["##........"] * 10 + ["##########"]),
    "E": box(10, 12, ["##########"] + ["##........"] * 4 + ["##########"] * 2 + ["##........"] * 4 + ["##########"]),
    "W": box(10, 12, ["#...##...#"] * 11 + ["##########"]),
    "Q": box(12, 12, ["##.....##..."] * 10 + ["############"] * 2),
    "J": box(10, 12, ["###......."] * 6 + ["##########"] * 6),
    "T": box(10, 12, ["##########"] * 2 + ["....##...."] * 10),
    "3": box(10, 12, ["##########"] * 12),
    "4": box(10, 12, ["#.#.#.#.#."] * 12),
    "6": box(10, 12, [".#.#.#.#.#"] * 12),
    "\xcb": box(10, 16, ["##########"] * 16),
}


def font(glyphs, extra_codes="cbyzjewq"):
    """Font bytes: every glyph on its own row of the atlas, plus records for codes
    that only point elsewhere until the rebuild sets them."""
    b = bytearray(TEX + 128 + W * H)
    b[:4] = b"4.1v"
    struct.pack_into("<I", b, 4, TEX)
    struct.pack_into("<I", b, TEX + 12, russian_font.DXT3 << 8 | 7 << 20 | 7 << 24)
    atlas = [[0] * W for _ in range(H)]
    at = 0x220
    for n, (ch, bitmap) in enumerate(glyphs.items()):
        x, y = 2 + (n % 6) * 14, 2 + (n // 6) * 20
        for j, row in enumerate(bitmap):
            atlas[y + j][x:x + len(row)] = row
        top = 0.02 if ch == "\xcb" else 0.05
        struct.pack_into("<7fH", b, at, x / W, y / H, len(bitmap[0]) / W, len(bitmap) / H, 0.0, top, 11 / W, ord(ch))
        at += 40
    for ch in extra_codes:
        struct.pack_into("<7fH", b, at, 0, 0, 0, 0, 0, 0, 0, ord(ch))
        at += 40
    f = russian_font.Font(b)
    f.put_alpha(atlas)
    for code in f.recs:
        if code < 0x100 and code != 0 and chr(code) in glyphs:
            f.whiten(*f.pixel_rect(code))
    return bytes(f.b)


class RussianFont(unittest.TestCase):
    def setUp(self):
        self.ru = font(GLYPHS)
        english = dict(GLYPHS)
        english["4"] = box(10, 12, ["....####.."] * 12)
        english["6"] = box(10, 12, ["####......"] * 12)
        english["3"] = box(10, 12, ["......####"] * 12)
        self.en = font(english)

    def test_encode_uses_lowercase_cells_for_new_and_moved_letters(self):
        self.assertEqual(russian_font.encode("Лейтенант, 07:36"), "LEjTEHAHT, 07:36")
        self.assertEqual(russian_font.encode("ЗАЩИЩЁН ОБЪЕКТ Э"), "zAwNwyH ObqEKT e")
        self.assertEqual(russian_font.encode("АК-47 %1 "), "AK-47 %1 ")

    def test_alpha_round_trips(self):
        f = russian_font.Font(self.ru)
        a = f.alpha()
        a[5][7] = 9
        f.put_alpha(a)
        self.assertEqual(f.alpha(), a)

    def test_rebuild_keeps_layout_and_draws_new_glyphs(self):
        data, placed = russian_font.rebuild(self.ru, self.en, ["  @@  @@  ", "   @@@@   "])
        self.assertEqual(len(data), len(self.ru))
        self.assertEqual(data[:0x220], self.ru[:0x220])
        self.assertEqual(sorted(placed), sorted("346jewq"))
        old, new = russian_font.Font(self.ru), russian_font.Font(data)
        self.assertEqual(sorted(old.recs), sorted(new.recs))
        a = new.alpha()

        def bitmap(code):
            return russian_font.crop(a, new.pixel_rect(ord(code)))

        # Digits come from the English font; Ч and Б keep the translation's 4 and 6.
        self.assertEqual(bitmap("4"), [[15 if c == "#" else 0 for c in "....####.."]] * 12)
        self.assertEqual(new.rec(ord("c")), old.rec(ord("4")))
        self.assertEqual(new.rec(ord("b")), old.rec(ord("6")))
        self.assertEqual(new.rec(ord("z")), new.rec(ord("3")))
        self.assertEqual(new.rec(ord("y")), old.rec(0xCB))
        # Й is И under a breve in Ë's accent rows; Щ and Ъ are wider than Ш and Ь.
        j = bitmap("j")
        self.assertEqual(j[-12:], GLYPHS["N"])
        self.assertEqual(j[0], [15 if c == "@" else 0 for c in "  @@  @@  "])
        self.assertEqual(new.rec(ord("j"))[5], old.rec(0xCB)[5])
        self.assertEqual(len(bitmap("w")[0]), 10 + 3)
        self.assertEqual(bitmap("w")[-1][-3:], [15, 15, 15])
        self.assertEqual(len(bitmap("q")[0]), 10 + 3)
        self.assertGreater(new.rec(ord("q"))[6], old.rec(ord("J"))[6])
        # Э: С mirrored, its bar only on the right half.
        e = bitmap("e")
        self.assertEqual(e[5], [0] * 5 + [15] * 5)
        self.assertEqual(e[1], [0] * 8 + [15] * 2)
        # New glyphs never overlap a glyph still in use.
        used = [old.pixel_rect(c) for c in old.recs if c in map(ord, "NCEWQJT\xcb")]
        for x, y, w, h in placed.values():
            for ux, uy, uw, uh in used:
                self.assertTrue(x + w < ux or ux + uw < x or y + h < uy or uy + uh < y)

    def test_rebuild_reports_a_full_atlas(self):
        crowded = russian_font.Font(font(GLYPHS, "cbyzjewq\u0100"))
        crowded.set_rec(0x100, [0, 0, 1, 1, 0, 0, 0])      # one glyph claims the whole atlas
        with self.assertRaises(ValueError):
            russian_font.rebuild(bytes(crowded.b), self.en, ["@@"])


if __name__ == "__main__":
    unittest.main()
