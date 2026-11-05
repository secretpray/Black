import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from inspect_profile import folder_name, inspect, read_regular


class ProfileInspection(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.addCleanup(self.temp.cleanup)

    def profile(self, name="ПРОФИЛЬ"):
        legacy = name.encode("utf-8").decode("latin-1")
        folder = self.root / folder_name(legacy)
        folder.mkdir()
        profile = folder / name
        # Fixed double-zero-key HMAC-SHA1 vector over a zeroed 0x17EC payload.
        profile.write_bytes(bytes(0x17EC) + bytes.fromhex("d2e44358b12028cc9f6710dea76e25b8dae79fb5"))
        (folder / "SaveMeta.xbx").write_bytes(("\ufeffName=" + legacy + "\r\nNoCopy=1\r\n").encode("utf-16le"))
        return profile

    def test_unicode_and_legacy_folder_identity(self):
        self.assertEqual(folder_name("Profile"), "1AAF1A0557B7")
        self.assertEqual(folder_name("ПРОФИЛЬ"), "F778F73E01FF")
        profile = self.profile()
        result = inspect(profile)
        self.assertEqual(result["folder_from_legacy_utf8_bytes"], "03A47B43D696")
        self.assertTrue(result["double_zero_hmac_matches"])
        self.assertTrue(result["folder_matches_metadata"])
        self.assertTrue(result["metadata_name_is_legacy_utf8_bytes"])
        self.assertNotEqual(result["folder_from_unicode_filename"], result["folder_from_metadata"])
        # UTF-8 0x85 is widened to U+0085, not an XAPI metadata newline.
        self.assertTrue(inspect(self.profile("Х"))["folder_matches_metadata"])

    def test_changed_signature_and_size_are_not_accepted(self):
        profile = self.profile("Profile")
        data = bytearray(profile.read_bytes()); data[0] = 1; profile.write_bytes(data)
        self.assertFalse(inspect(profile)["double_zero_hmac_matches"])
        for size in (0, 0x17FF, 0x1801):
            profile.write_bytes(bytes(size))
            with self.assertRaises(ValueError): inspect(profile)

    def test_symlinks_special_files_and_ambiguous_metadata_rejected(self):
        profile = self.profile()
        link = self.root / "link"; link.symlink_to(profile)
        with self.assertRaises(OSError): inspect(link)
        with self.assertRaises(ValueError): read_regular(self.root, 65536)
        fifo = self.root / "fifo"; os.mkfifo(fifo)
        with self.assertRaises(ValueError): read_regular(fifo, 65536)
        meta = profile.parent / "SaveMeta.xbx"
        meta.write_bytes("Name=A\nName=B\n".encode("utf-16le"))
        with self.assertRaises(ValueError): inspect(profile)
        meta.unlink(); meta.symlink_to(profile)
        with self.assertRaises(OSError): inspect(profile)

    def test_new_process_leaves_profile_and_metadata_identical(self):
        profile = self.profile()
        before = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in profile.parent.iterdir()}
        result = subprocess.run([sys.executable, str(Path(__file__).with_name("inspect_profile.py")), str(profile)],
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        after = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in profile.parent.iterdir()}
        self.assertEqual(before, after)


if __name__ == "__main__":
    unittest.main()
