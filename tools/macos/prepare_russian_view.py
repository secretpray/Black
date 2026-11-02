#!/usr/bin/env python3
"""Build a local Russian test view from a user's game and translation files."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import tempfile

SUBTITLE_STEMS = ('03_N', '05_N', '07_N', '09_N', '11_N', '13_N', '15_N', '17_N', '18_N')


def validate_srt(content):
    text = content.decode('utf-8-sig').replace('\r\n', '\n').replace('\r', '\n')
    blocks = re.split(r'\n\s*\n', text.strip())
    stamp = re.compile(r'^(\d{2}):(\d{2}):(\d{2}),(\d{3}) --> (\d{2}):(\d{2}):(\d{2}),(\d{3})$')
    previous = -1
    for block in blocks:
        lines = block.split('\n')
        if len(lines) < 3 or not lines[0].isdigit() or not any(line.strip() for line in lines[2:]):
            raise ValueError('Malformed subtitle cue')
        match = stamp.fullmatch(lines[1].strip())
        if not match:
            raise ValueError('Malformed subtitle timestamp')
        v = list(map(int, match.groups()))
        if any(v[i] >= 60 for i in (1, 2, 5, 6)):
            raise ValueError('Invalid subtitle timestamp')
        start = ((v[0]*60+v[1])*60+v[2])*1000+v[3]
        end = ((v[4]*60+v[5])*60+v[6])*1000+v[7]
        if start < previous or end <= start:
            raise ValueError('Out-of-order subtitle cue')
        previous = start
    if not blocks:
        raise ValueError('Empty subtitle track')
    return len(blocks)


def translated_bank(original, lines):
    data = original
    if len(data) < 16:
        raise ValueError('String bank header is truncated')
    count, table = struct.unpack_from('<II', data, 8)
    if table != 16 or count > 100000 or table + 4 * count > len(data):
        raise ValueError('Invalid string bank table')
    translations = {}
    for line in lines.splitlines():
        record = json.loads(line)
        if 'id' not in record:
            continue
        key = int(record['id'], 16)
        text = record['text']
        if key in translations or not isinstance(text, str) or '\0' in text:
            raise ValueError('Duplicate or invalid translation')
        # The catalog preserves the original byte stream as Latin-1 codepoints;
        # some game glyph/control sequences are represented by several bytes.
        translations[key] = text.encode('latin-1')
    offsets = list(struct.unpack_from(f'<{count}I', data, table))
    output = bytearray(data[:table + count * 4])
    ids = set()
    for i, start in enumerate(offsets):
        end = offsets[i+1] if i+1 < count else len(data)
        if start < table + count * 4 or start + 5 > end or end > len(data):
            raise ValueError('Invalid string bank entry')
        key = struct.unpack_from('<I', data, start)[0]
        if key in ids or key not in translations or b'\0' not in data[start+4:end]:
            raise ValueError('Missing, duplicate or malformed string')
        ids.add(key)
        struct.pack_into('<I', output, table + i * 4, len(output))
        output.extend(data[start:start+4]); output.extend(translations[key]); output.append(0)
        if i+1 < count:
            output.extend(b'\0' * (-len(output) % 4))
    if set(translations) != ids:
        raise ValueError('Translation IDs do not exactly match the game')
    return bytes(output)


def prepare(game, language, output):
    game, language, output = game.resolve(strict=True), language.resolve(strict=True), output.absolute()
    if output.exists() or output.is_symlink():
        raise ValueError('Output already exists')
    if output.resolve().is_relative_to(game) or output.resolve().is_relative_to(language):
        raise ValueError('Output must be outside source folders')
    bank = translated_bank((game/'language/strings/MainUS.bin').read_bytes(), (language/'menu.jsonl').read_text(encoding='utf-8'))
    fonts = {name: (language/'fonts'/name).read_bytes() for name in ('Big.bin','Small.bin')}
    if any(len(v) < 0x220 or v[:4] != b'4.1v' for v in fonts.values()):
        raise ValueError('Invalid font header')
    subtitles = {}
    if (language/'subtitles').is_dir():
        for stem in SUBTITLE_STEMS:
            content = (language/'subtitles'/f'{stem}.srt').read_bytes()
            validate_srt(content)
            subtitles[stem] = content
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.russian-view-', dir=output.parent) as tmp:
        view=Path(tmp)/'view';view.mkdir()
        for item in game.iterdir():
            if item.name.lower() != 'language': (view/item.name).symlink_to(item.resolve(), target_is_directory=item.is_dir())
        lang=view/'language';lang.mkdir()
        for item in (game/'language').iterdir():
            if item.name.lower() not in ('strings','fonts'): (lang/item.name).symlink_to(item.resolve(), target_is_directory=item.is_dir())
        for sub in ('strings','fonts'):
            folder=lang/sub;folder.mkdir()
            for item in (game/'language'/sub).iterdir():
                if (sub=='strings' and item.name.lower()=='mainus.bin') or (sub=='fonts' and item.name.lower() in ('big.bin','small.bin')):continue
                (folder/item.name).symlink_to(item.resolve(), target_is_directory=item.is_dir())
        (lang/'strings/MainUS.bin').write_bytes(bank)
        for name,content in fonts.items(): (lang/'fonts'/name).write_bytes(content)
        if subtitles:
            folder = view/'subtitles'/'ru'; folder.mkdir(parents=True)
            for name, content in subtitles.items(): (folder/f'{name}.srt').write_bytes(content)
        view.rename(output)
    print(f'Prepared local test view; {len(bank)} bank bytes; {len(subtitles)} subtitle tracks; SHA-256 {hashlib.sha256(bank).hexdigest()}')

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game',type=Path);parser.add_argument('language',type=Path);parser.add_argument('output',type=Path)
    args=parser.parse_args();prepare(args.game,args.language,args.output)
