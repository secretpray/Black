import struct
import tempfile
from pathlib import Path
import unittest
from analyze_audio import repeated_packets

class PacketReplay(unittest.TestCase):
    def test_varying_replay_excludes_silence_and_stereo_dc(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "audio.s16le"
            moving = struct.pack("<512h", *range(512))
            zero = bytes(1024)
            dc = struct.pack("<hh", 700, -800) * 256
            path.write_bytes(moving * 3 + zero * 4 + dc * 4 + moving)
            result = repeated_packets(path)
            self.assertEqual(result["complete_packets"], 12)
            self.assertEqual(result["repeated_varying_packets"], 2)
            self.assertEqual(result["longest_varying_run_packets"], 3)
            self.assertEqual(result["first_spans"][0]["repeated_packets"], 2)
            self.assertAlmostEqual(result["replayed_seconds"], 512 / 48000)

    def test_changing_packets_and_partial_tail_are_not_replay(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "audio.s16le"
            path.write_bytes(b"".join(struct.pack("<512h", *(i + n for n in range(512))) for i in range(3)) + b"\0\0\0\0")
            result = repeated_packets(path)
            self.assertEqual(result["complete_packets"], 3)
            self.assertEqual(result["repeated_varying_packets"], 0)
            # The cap on displayed spans must not merge later repeats into
            # the last stored span; aggregate counts still include them all.
            path.write_bytes(b"".join(struct.pack("<512h", *(i + n for n in range(512))) * 3 for i in range(35)))
            result = repeated_packets(path)
            self.assertEqual(result["repeated_varying_packets"], 70)
            self.assertEqual(len(result["first_spans"]), 30)
            self.assertTrue(all(s["repeated_packets"] == 2 for s in result["first_spans"]))
