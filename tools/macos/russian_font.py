#!/usr/bin/env python3
"""Rebuild a Russian Big.bin/Small.bin game font for full Cyrillic.

Russian fan translations of BLACK paint Cyrillic over the Latin and digit cells
of the game's '4.1v' fonts, so they have no Й Э Щ Ъ Ё, and З Ч Б take the
cells of the digits 3 4 6. This tool keeps that font's file layout and code
table and changes only glyph records and atlas pixels:

- the digits 3 4 6 get the pixels of the game's own English font (same layout);
- Ч Б move to the lowercase cells c b, and З shares the digit 3, which reads as
  З in this square face (the usual painted З, flat on top, reads as Э);
- new Й Э Щ Ъ are drawn from И С Ш Ь (with Ц Т Е for their details) into free
  atlas space, on the lowercase cells j e w q; Ё uses Ë on y.

Text for the rebuilt font is written with encode() / CYRILLIC_CELLS. No font or
translation is kept in this repository; both come from your own copies:

    python3 tools/macos/russian_font.py <russian fonts dir> \
        out/macos/normalized-game/language/fonts <output dir>
"""
import argparse
from pathlib import Path
import struct

FONTS = ("Big.bin", "Small.bin")

# Latin cell for each Cyrillic letter in the rebuilt font. The uppercase cells
# hold the translation's own Cyrillic; lowercase Latin duplicates them there, so
# the lowercase cells below are free for the moved and new letters.
CYRILLIC_CELLS = dict(zip("АВСДЕГЖНЬКЛМИОРЦЯЫТЮФШХУП", "ABCDEFGHJKLMNOPQRSTUVWXYZ"))
CYRILLIC_CELLS.update({"З": "z", "Ч": "c", "Б": "b", "Й": "j", "Э": "e", "Щ": "w", "Ъ": "q", "Ё": "y"})

# Breve rows drawn over И for Й, by font.
BREVE = {
    "Big.bin": ["  @@     @@  ", "  @@@   @@@  ", "   @@@@@@@   ", "    @@@@@    "],
    "Small.bin": ["  @@   @@  ", "   @@@@@   "],
}
RECORDS = 0x220
RECORD_SIZE = 40
DXT3 = 0x0E
# Accented Latin that Russian text does not use; its atlas cells may be reused.
UNUSED = set(range(0xC0, 0x100)) | {0x152, 0x153, 0x178}
DIAERESIS_E = 0xCB


def encode(text):
    """Game string for Cyrillic text in the rebuilt font. Characters with no
    Cyrillic cell (digits, punctuation, button glyphs) are left as they are."""
    return "".join(CYRILLIC_CELLS.get(ch.upper(), ch) for ch in text)


class Font:
    """'4.1v' font: 40-byte glyph records from 0x220 (seven floats u, v, w, h,
    left, top, advance in atlas units, then a u16 code at +28) and a DXT3 atlas
    whose colour is white, so only its 4-bit alpha carries the glyphs."""

    def __init__(self, data):
        self.b = bytearray(data)
        if self.b[:4] != b"4.1v":
            raise ValueError("not a 4.1v font")
        self.tex = struct.unpack_from("<I", self.b, 4)[0]
        fmt = struct.unpack_from("<I", self.b, self.tex + 12)[0]
        if (fmt >> 8) & 0xFF != DXT3:
            raise ValueError("font atlas is not DXT3")
        self.W = 1 << ((fmt >> 20) & 15)
        self.H = 1 << ((fmt >> 24) & 15)
        self.data = self.tex + 128
        self.recs = {}
        for at in range(RECORDS, self.tex - RECORD_SIZE + 1, RECORD_SIZE):
            code = struct.unpack_from("<H", self.b, at + 28)[0]
            self.recs.setdefault(code, []).append(at)

    def rec(self, code):
        return list(struct.unpack_from("<7f", self.b, self.recs[code][0]))

    def set_rec(self, code, values):
        for at in self.recs.get(code, ()):
            struct.pack_into("<7f", self.b, at, *values)

    def pixel_rect(self, code):
        u, v, w, h = self.rec(code)[:4]
        return round(u * self.W), round(v * self.H), round(w * self.W), round(h * self.H)

    def _block(self, bx, by):
        return self.data + (by * (self.W // 4) + bx) * 16

    def alpha(self):
        a = [[0] * self.W for _ in range(self.H)]
        for by in range(self.H // 4):
            for bx in range(self.W // 4):
                blk = self._block(bx, by)
                for i in range(16):
                    a[by * 4 + i // 4][bx * 4 + i % 4] = (self.b[blk + i // 2] >> ((i & 1) * 4)) & 15
        return a

    def put_alpha(self, a):
        for by in range(self.H // 4):
            for bx in range(self.W // 4):
                blk = self._block(bx, by)
                for k in range(8):
                    lo = a[by * 4 + (2 * k) // 4][bx * 4 + (2 * k) % 4]
                    hi = a[by * 4 + (2 * k + 1) // 4][bx * 4 + (2 * k + 1) % 4]
                    self.b[blk + k] = (lo & 15) | ((hi & 15) << 4)

    def whiten(self, x, y, w, h):
        """White colour for every 4x4 block the rectangle (and its margin) touches."""
        for by in range(max(0, (y - 1) // 4), min(self.H // 4, (y + h + 1) // 4 + 1)):
            for bx in range(max(0, (x - 1) // 4), min(self.W // 4, (x + w + 1) // 4 + 1)):
                blk = self._block(bx, by)
                self.b[blk + 8:blk + 16] = b"\xff\xff\xff\xff\x00\x00\x00\x00"


def crop(a, rect):
    x, y, w, h = rect
    return [row[x:x + w] for row in a[y:y + h]]


def glyph(font, atlas, code):
    """(bitmap, left, top, advance) of a code."""
    _, _, _, _, left, top, advance = font.rec(code)
    return crop(atlas, font.pixel_rect(code)), left, top, advance


def new_glyphs(ru, a, en, breve):
    """Bitmaps and metrics for the digits and the new letters, by code."""
    W, H = ru.W, ru.H
    ae = en.alpha()
    out = {ord(d): glyph(en, ae, ord(d)) for d in "346"}

    # Й: И with a breve in the accent rows Ë uses above E.
    nb, nl, _, nadv = glyph(ru, a, ord("N"))
    accent_top = ru.rec(DIAERESIS_E)[5]
    dh = round((ru.rec(ord("N"))[5] - accent_top) * H)
    width = len(nb[0])
    top = [[15 if ch == "@" else 0 for ch in row.ljust(width)[:width]] for row in breve]
    top += [[0] * width for _ in range(dh - len(top))]
    out[ord("j")] = (top + nb, nl, accent_top, nadv)

    # Э: С mirrored, with a short bar from the middle so it stays apart from З/3.
    cb, cl, ct, cadv = glyph(ru, a, ord("C"))
    eb = glyph(ru, a, ord("E"))[0]
    mirrored = [list(reversed(row)) for row in cb]
    w, h = len(mirrored[0]), len(mirrored)
    for j, row in enumerate(eb):
        if h // 3 <= j <= 2 * h // 3 and sum(v >= 12 for v in row) >= len(row) * 0.6:
            for i in range(w // 2, w):
                mirrored[j][i] = 15
    out[ord("e")] = (mirrored, cl, ct, cadv)

    # Щ: Ш whose bottom bar runs on to the right, as Ц's does past its stem.
    wb, wl, wt, wadv = glyph(ru, a, ord("W"))
    qb = glyph(ru, a, ord("Q"))[0]
    stem_end = max(i for i, v in enumerate(qb[len(qb) // 2]) if v >= 8)
    tail = len(qb[0]) - 1 - stem_end
    shch = [row + [0] * tail for row in wb]
    for j, row in enumerate(qb):
        if row[-1] >= 4:
            shch[j][len(wb[0]):] = row[stem_end + 1:]
    out[ord("w")] = (shch, wl, wt, wadv + tail / W)

    # Ъ: Ь moved right under the left end of Т's top bar.
    jb, jl, jt, jadv = glyph(ru, a, ord("J"))
    tb = glyph(ru, a, ord("T"))[0]
    stem = next(i for i, v in enumerate(jb[1]) if v < 8)
    hard = [[0] * stem + row for row in jb]
    for j, row in enumerate(tb):
        if row[0] >= 4:
            for i in range(2 * stem):
                hard[j][i] = max(hard[j][i], row[0])
    out[ord("q")] = (hard, jl, jt, jadv + stem / W)
    return out


def rebuild(ru_data, en_data, breve):
    """Rebuilt font bytes and {character: (x, y, w, h)} of the drawn glyphs."""
    ru, en = Font(ru_data), Font(en_data)
    W, H = ru.W, ru.H
    a = ru.alpha()
    glyphs = new_glyphs(ru, a, en, breve)

    # Ч and Б keep the pixels painted in the 4 and 6 cells; Ё draws Ë.
    for letter, digit in (("c", "4"), ("b", "6")):
        ru.set_rec(ord(letter), ru.rec(ord(digit)))
    ru.set_rec(ord("y"), ru.rec(DIAERESIS_E))

    # Free space is everything outside the glyphs still in use, with a margin.
    busy = [[False] * W for _ in range(H)]
    for code in ru.recs:
        if code in UNUSED and code != DIAERESIS_E:
            continue
        x, y, w, h = ru.pixel_rect(code)
        for j in range(max(0, y - 1), min(H, y + h + 1)):
            for i in range(max(0, x - 1), min(W, x + w + 1)):
                busy[j][i] = True

    def free_spot(bw, bh):
        for y in range(H - bh - 1):
            for x in range(W - bw - 1):
                if not any(busy[j][i] for j in range(y, y + bh + 2) for i in range(x, x + bw + 2)):
                    return x, y
        return None

    placed = {}
    for code in sorted(glyphs, key=lambda c: -len(glyphs[c][0][0]) * len(glyphs[c][0])):
        bitmap, left, top, advance = glyphs[code]
        bw, bh = len(bitmap[0]), len(bitmap)
        spot = free_spot(bw, bh)
        if spot is None:
            raise ValueError(f"no atlas space for {chr(code)} ({bw}x{bh})")
        x, y = spot
        for j in range(y, y + bh + 2):
            for i in range(x, x + bw + 2):
                busy[j][i] = True
                a[j][i] = 0
        for j, row in enumerate(bitmap):
            a[y + 1 + j][x + 1:x + 1 + bw] = row
        ru.set_rec(code, [(x + 1) / W, (y + 1) / H, bw / W, bh / H, left, top, advance])
        placed[chr(code)] = (x + 1, y + 1, bw, bh)
    ru.put_alpha(a)
    ru.set_rec(ord("z"), ru.rec(ord("3")))
    for rect in placed.values():
        ru.whiten(*rect)
    return bytes(ru.b), placed


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("russian", type=Path, help="folder with the translation's Big.bin and Small.bin")
    parser.add_argument("english", type=Path, help="folder with the game's own Big.bin and Small.bin")
    parser.add_argument("output", type=Path)
    args = parser.parse_args(argv)
    args.output.mkdir(parents=True, exist_ok=True)
    for name in FONTS:
        data, placed = rebuild((args.russian / name).read_bytes(), (args.english / name).read_bytes(), BREVE[name])
        (args.output / name).write_bytes(data)
        print(name, " ".join(f"{ch}@{x},{y}" for ch, (x, y, _, _) in placed.items()))


if __name__ == "__main__":
    main()
