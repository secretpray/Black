import struct
import tempfile
from pathlib import Path
import unittest
from analyze_audio import analyze_float_mix

class FloatMix(unittest.TestCase):
    def test_overload_and_nonfinite_before_clipping(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "mix.f32le"
            path.write_bytes(struct.pack("<6f", 1.59, -1.2, .2, float("nan"), .1, float("inf")))
            result = analyze_float_mix(path)
            self.assertEqual(result["frames"], 3)
            self.assertEqual(result["over_full_scale"], 2)
            self.assertEqual(result["nonfinite"], 2)
            self.assertAlmostEqual(result["peak"], 1.59, places=5)

    def test_partial_stereo_frame_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "mix.f32le"
            path.write_bytes(struct.pack("<f", .1))
            with self.assertRaises(ValueError): analyze_float_mix(path)
