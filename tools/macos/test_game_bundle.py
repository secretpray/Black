from pathlib import Path
import plistlib
import tempfile
import unittest
from unittest.mock import patch
import game_bundle


class GameBundle(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.binary = Path(self.temp.name) / "black-recomp"
        self.binary.write_bytes(b"synthetic binary, no game data")

    def test_game_identity_and_original_preservation(self):
        before = self.binary.read_bytes()
        executable, _ = game_bundle.prepare(self.binary, sign=False)
        info = plistlib.loads((executable.parents[1] / "Info.plist").read_bytes())
        self.assertTrue(info["LSSupportsGameMode"])
        self.assertEqual(info["LSApplicationCategoryType"], "public.app-category.action-games")
        self.assertEqual(info["CFBundleExecutable"], executable.name)
        self.assertEqual(info["CFBundleIconFile"], "BLACK.icns")
        self.assertEqual((executable.parents[1] / "Resources/BLACK.icns").read_bytes(), game_bundle.ICON.read_bytes())
        self.assertEqual(self.binary.read_bytes(), before)
        self.assertEqual(executable.read_bytes(), before)

    def test_reuse_rebuild_and_tamper_detection(self):
        executable, first = game_bundle.prepare(self.binary, sign=False)
        modified = executable.stat().st_mtime_ns
        self.assertEqual(game_bundle.prepare(self.binary, sign=False)[1], first)
        self.assertEqual(executable.stat().st_mtime_ns, modified)
        executable.write_bytes(b"changed copy")
        self.assertEqual(game_bundle.prepare(self.binary, sign=False)[0].read_bytes(), self.binary.read_bytes())
        self.binary.write_bytes(b"new build")
        self.assertNotEqual(game_bundle.prepare(self.binary, sign=False)[1]["source_sha256"], first["source_sha256"])

    def test_failed_signing_preserves_previous_app_and_source(self):
        executable, _ = game_bundle.prepare(self.binary, sign=False)
        before = executable.read_bytes()
        self.binary.write_bytes(b"new source")
        with patch.object(game_bundle.subprocess, "run", side_effect=RuntimeError("sign failed")):
            with self.assertRaises(RuntimeError):
                game_bundle.prepare(self.binary)
        self.assertEqual(executable.read_bytes(), before)
        self.assertEqual(self.binary.read_bytes(), b"new source")
