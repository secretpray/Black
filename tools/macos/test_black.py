"""Disc/revision guards and metadata extraction, using synthetic data only."""
import hashlib
import io
from pathlib import Path
import struct
import tarfile
import tempfile
import unittest
from unittest.mock import patch

import black
from analyze_audio import analyze


def synthetic_xbe(title=0x45410083, entry=0x25A3F):
    data = bytearray(0x200)
    data[:4] = b"XBEH"
    struct.pack_into("<I", data, 0x104, 0x10000)
    struct.pack_into("<I", data, 0x118, 0x10180)
    struct.pack_into("<I", data, 0x128, entry ^ 0xA8FC57AB)
    struct.pack_into("<I", data, 0x188, title)
    return data


class Renderer(unittest.TestCase):
    def parse(self, *argv):
        import argparse
        parser = argparse.ArgumentParser()
        black.add_renderer(parser)
        return parser.parse_args(list(argv))

    def test_metal_is_the_default_and_opengl_is_explicit(self):
        self.assertTrue(self.parse().metal)
        self.assertTrue(self.parse("--metal").metal)
        self.assertFalse(self.parse("--opengl").metal)
        with self.assertRaises(SystemExit), patch("sys.stderr", io.StringIO()):
            self.parse("--metal", "--opengl")

    def test_missing_angle_names_the_opengl_fallback(self):
        with tempfile.TemporaryDirectory() as empty, patch.dict("os.environ", {"BLACK_ANGLE_LIB_DIR": empty}):
            with self.assertRaisesRegex(ValueError, "--opengl"):
                black.preset(self.parse())
        self.assertEqual(black.preset(self.parse("--opengl")), "macos-arm64")


class DiscGuards(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.xbe = self.root / "default.xbe"

    def test_wrong_magic_or_truncated_header(self):
        for data in (b"XBEH", b"not an xbe" * 100):
            self.xbe.write_bytes(data)
            with self.assertRaisesRegex(ValueError, "Not an Xbox XBE"):
                black.validate_xbe(self.xbe)

    def test_saved_graphics_defaults_validation_and_atomic_replacement(self):
        path = self.root / "settings.ini"
        defaults = {"render_scale": 2, "field_of_view": 70, "anti_aliasing": 2, "anisotropic_filtering": 0,
                    "upscaler": 1}
        self.assertEqual(black.launch_graphics(path), defaults)
        path.write_text("skip_intro_movies=1\nrender_scale=3\nfield_of_view=85\n")
        self.assertEqual(black.launch_graphics(path), {**defaults, "render_scale": 3, "field_of_view": 85})
        staged = self.root / "replacement"
        staged.write_text("render_scale=0\nrender_scale=7\nrender_scale=3 extra\nfield_of_view=141\nfield_of_view=45oops\n"
                          "anti_aliasing=3\nanisotropic_filtering=3\nanisotropic_filtering=32\nupscaler=2\nanti_aliasing=-1\n")
        staged.replace(path)
        self.assertEqual(black.launch_graphics(path), defaults)
        path.write_text("render_scale=1\nfield_of_view=45\nrender_scale=6\nfield_of_view=140\n"
                        "anti_aliasing=2\nanisotropic_filtering=16\nupscaler=1\n")
        self.assertEqual(black.launch_graphics(path),
                         {"render_scale": 6, "field_of_view": 140, "anti_aliasing": 2, "anisotropic_filtering": 16,
                          "upscaler": 1})

    def test_wrong_title_and_revision_rejected_before_generation(self):
        for data in (synthetic_xbe(title=1), synthetic_xbe(entry=0x12345)):
            self.xbe.write_bytes(data)
            with self.assertRaisesRegex(ValueError, "Unsupported BLACK revision"):
                black.validate_xbe(self.xbe)

    def test_out_of_file_certificate_is_rejected(self):
        data = synthetic_xbe()
        struct.pack_into("<I", data, 0x118, 0xFFFFFFFF)
        self.xbe.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "certificate"):
            black.validate_xbe(self.xbe)

    def test_correct_headers_do_not_bypass_revision_hash(self):
        self.xbe.write_bytes(synthetic_xbe())
        with self.assertRaisesRegex(ValueError, "Unsupported executable SHA-256"):
            black.validate_xbe(self.xbe)

    def test_supported_hash_is_required_and_input_is_unchanged(self):
        data = synthetic_xbe()
        self.xbe.write_bytes(data)
        expected = hashlib.sha256(data).hexdigest()
        with patch.object(black, "XBE_SHA256", expected):
            self.assertEqual(black.validate_xbe(self.xbe), expected)
        self.assertEqual(self.xbe.read_bytes(), data)

    def archive(self, symlink=False):
        archive = self.root / "analysis.tar.xz"
        with tarfile.open(archive, "w:xz") as target:
            for name in (*black.ANALYSIS_FILES, "../../escaped.json"):
                member = tarfile.TarInfo(name)
                if symlink and name == black.ANALYSIS_FILES[0]:
                    member.type = tarfile.SYMTYPE
                    member.linkname = "/tmp/black-analysis"
                    target.addfile(member)
                else:
                    member.size = 2
                    target.addfile(member, io.BytesIO(b"[]"))
        return archive

    def test_only_named_regular_metadata_files_are_extracted(self):
        output = self.root / "metadata"
        black.extract_analysis(self.archive(), output)
        files = sorted(str(p.relative_to(output)) for p in output.rglob("*.json"))
        self.assertEqual(files, sorted(black.ANALYSIS_FILES))
        self.assertFalse((self.root / "escaped.json").exists())

    def test_metadata_symlink_rejected(self):
        with self.assertRaisesRegex(ValueError, "regular metadata"):
            black.extract_analysis(self.archive(symlink=True), self.root / "metadata")

    def test_europe_patch_changes_extra_bytes_and_backward_seek(self):
        patch_bytes = b"\x03\x01\x01\x01\x02XY\x01\x03\x00\x00\x00"
        self.assertEqual(black.apply_europe_patch(b"abcde", patch_bytes), b"accXYcde")

    def test_corrupt_europe_patch_is_rejected(self):
        for data in (b"\x80", b"\x80" * 10, b"\x06\x00", b"\x03\x01\x09\x00",
                     b"\x00\x00\x04a"):
            with self.assertRaises(ValueError):
                black.apply_europe_patch(b"abcde", data)

    def test_europe_asset_alias_does_not_modify_original_tree(self):
        original = self.root / "disc"
        strings = original / "language/strings"
        strings.mkdir(parents=True)
        (strings / "MainUK.bin").write_bytes(b"synthetic strings")
        (original / "default.xbe").write_bytes(b"original")
        view = self.root / "view"
        view.mkdir()
        black.link_disc_assets(original, view)
        self.assertEqual((view / "language/strings/MainUS.bin").read_bytes(), b"synthetic strings")
        self.assertFalse((strings / "MainUS.bin").exists())
        self.assertEqual((original / "default.xbe").read_bytes(), b"original")
        self.assertFalse((view / "default.xbe").exists())


class AudioCaptureChecks(unittest.TestCase):
    def test_silence_and_full_scale_signed_pcm(self):
        with tempfile.TemporaryDirectory() as folder:
            pcm = Path(folder) / "capture.s16le"
            pcm.write_bytes(bytes(48000 * 4) + struct.pack("<hhhh", -32768, 32767, 0, 0))
            report = analyze(pcm)
            self.assertEqual(report["frames"], 48002)
            self.assertIsNone(report["seconds"][0]["rms_dbfs"])
            self.assertEqual(report["seconds"][0]["nonzero_samples"], 0)
            self.assertEqual(report["nonzero_samples"], 2)
            self.assertEqual(report["clipped_samples"], 2)
            self.assertEqual(report["peak"], 32768)

    def test_incomplete_stereo_frame_is_not_reported_as_valid_audio(self):
        with tempfile.TemporaryDirectory() as folder:
            pcm = Path(folder) / "capture.s16le"
            pcm.write_bytes(b"\x01\x00")
            with self.assertRaisesRegex(ValueError, "Partial stereo"):
                analyze(pcm)


if __name__ == "__main__":
    unittest.main()
