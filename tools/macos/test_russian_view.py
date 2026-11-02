import json
import struct
import unittest
from prepare_russian_view import translated_bank, validate_srt


def bank():
    return struct.pack('<IIIIII', 1, 2, 2, 16, 24, 32) + struct.pack('<I', 10) + b'ONE\0' + struct.pack('<I', 20) + b'TWO\0'


class RussianViewTest(unittest.TestCase):
    def test_subtitles_validate_utf8_timing_and_order(self):
        srt = '1\n00:00:01,000 --> 00:00:02,500\nПривет\n\n2\n00:00:03,000 --> 00:00:04,000\nМир\n'
        self.assertEqual(validate_srt(srt.encode()), 2)
        for bad in (srt.replace('00:00:02,500', '00:00:00,500'),
                    srt.replace('00:00:03,000', '00:00:00,000'),
                    srt.replace('00:00:01,000', '00:61:01,000')):
            with self.assertRaises(ValueError): validate_srt(bad.encode())

    def test_rebuild_preserves_ids_and_control_bytes(self):
        lines='\n'.join(json.dumps(x) for x in [{'id':'0000000A','text':'ABC'}, {'id':'00000014','text':'X\u00ef\u0080\u009e'}])
        out=translated_bank(bank(),lines)
        n,table=struct.unpack_from('<II',out,8)
        self.assertEqual(n,2)
        a,b=struct.unpack_from('<II',out,table)
        self.assertEqual(out[a:b],struct.pack('<I',10)+b'ABC\0')
        self.assertEqual(out[b:],struct.pack('<I',20)+b'X\xef\x80\x9e\0')

    def test_missing_duplicate_and_bad_offsets_are_rejected(self):
        line=json.dumps({'id':'0000000A','text':'ABC'})
        with self.assertRaises(ValueError): translated_bank(bank(),line)
        with self.assertRaises(ValueError): translated_bank(bank(),line+'\n'+line)
        broken=bytearray(bank());struct.pack_into('<I',broken,16,9999)
        with self.assertRaises(ValueError): translated_bank(bytes(broken),line)

if __name__ == '__main__': unittest.main()
