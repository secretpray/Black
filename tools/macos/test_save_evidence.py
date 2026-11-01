"""Preservation and launch failure/exit checks; no real saves or game process."""
import contextlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import Mock, patch

import black
import save_evidence


class SaveEvidence(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.saves = self.root / "out/macos/user/data/xboxrecomp"
        self.profile = self.saves / "UserData/45410083/slot/profile.dat"
        self.profile.parent.mkdir(parents=True)
        self.profile.write_bytes(b"synthetic profile\x00\xff")
        (self.saves / "TitleData/empty").mkdir(parents=True)
        self.run_dir = self.root / "evidence"
        self.run_dir.mkdir()

    def test_backup_preserves_bytes_empty_dirs_and_sparse_partition(self):
        with (self.saves / "Partition3.img").open("wb") as stream:
            stream.seek(8 * 1024 * 1024)
            stream.write(b"end")
        before = save_evidence.inventory(self.saves)
        self.assertEqual(save_evidence.backup(self.saves, self.run_dir), before)
        self.assertEqual(save_evidence.inventory(self.saves), before)
        report = json.loads((self.run_dir / "saves-before.json").read_text())
        self.assertTrue(report["verified"])
        archive = self.run_dir / report["archive"]
        self.assertEqual(report["archive_sha256"], save_evidence.sha256(archive))
        self.assertLess(archive.stat().st_size, 100000)
        # A real new directory round trip, never restoring over the source.
        restored = self.root / "restored"
        with tarfile.open(archive, "r:gz") as source:
            source.extractall(restored, filter="data")
        self.assertEqual(save_evidence.inventory(restored), before)

    def test_changes_distinguish_progress_write_from_unchanged_metadata(self):
        before = save_evidence.backup(self.saves, self.run_dir)
        self.profile.write_bytes(b"new synthetic progress")
        (self.saves / "UserData/new.dat").write_bytes(b"new")
        changes = save_evidence.record_after(self.saves, self.run_dir, before)
        self.assertEqual(changes, {"added": ["UserData/new.dat"], "removed": [],
                                   "changed": ["UserData/45410083/slot/profile.dat"]})
        self.profile.unlink()
        self.assertEqual(save_evidence.record_after(self.saves, self.run_dir, before)["removed"],
                         ["UserData/45410083/slot/profile.dat"])

    def test_absent_root_is_backed_up_without_creating_or_importing_saves(self):
        missing = self.root / "missing"
        before = save_evidence.backup(missing, self.run_dir)
        self.assertFalse(before["root_exists"])
        self.assertFalse(missing.exists())
        self.assertEqual(save_evidence.record_after(missing, self.run_dir, before),
                         {"added": [], "removed": [], "changed": []})

    def test_symlinks_corrupt_archive_and_concurrent_edits_are_rejected(self):
        link = self.saves / "link"
        link.symlink_to(self.profile)
        with self.assertRaisesRegex(ValueError, "symbolic link"):
            save_evidence.backup(self.saves, self.run_dir)
        link.unlink()
        before = save_evidence.backup(self.saves, self.run_dir)
        wrong = {**before, "files": {}}
        with self.assertRaisesRegex(ValueError, "SHA-256 manifest"):
            save_evidence.verify_archive(self.run_dir / "saves-before.tar.gz", wrong)
        other = self.root / "concurrent"
        other.mkdir()
        original = save_evidence.verify_archive

        def edit_after_verification(path, expected):
            original(path, expected)
            self.profile.write_bytes(b"external edit")

        with patch.object(save_evidence, "verify_archive", side_effect=edit_after_verification):
            with self.assertRaisesRegex(ValueError, "changed during backup"):
                save_evidence.backup(self.saves, other)
        self.assertFalse((other / "saves-before.json").exists())

    def launch(self, wait_effect=None, backup_error=None, dev=False):
        binary = self.root / "out/build/macos-arm64/black-recomp"
        binary.parent.mkdir(parents=True, exist_ok=True)
        binary.write_bytes(b"synthetic executable, never run")
        process = Mock(returncode=-15 if wait_effect else 0)

        def wait(timeout=None):
            if wait_effect:
                raise wait_effect
            self.profile.write_bytes(b"written by synthetic process")
            return 0

        process.wait.side_effect = wait
        arguments = ["black.py", "run", "--windowed", "--backup-saves", "--smoke-seconds", "10"]
        if dev:
            arguments += ["--dev-unlock-levels", "0"]
        with contextlib.ExitStack() as stack:
            stack.enter_context(patch.object(black, "ROOT", self.root))
            stack.enter_context(patch.object(black, "WORK", self.root / "out/macos"))
            stack.enter_context(patch.object(black, "provenance", return_value={"game_dir": str(self.root / "game")}))
            popen = stack.enter_context(patch.object(black.subprocess, "Popen", return_value=process))
            stopped = stack.enter_context(patch.object(black, "stop_child", return_value=False))
            if backup_error:
                stack.enter_context(patch.object(save_evidence, "backup", side_effect=backup_error))
            stack.enter_context(patch.object(sys, "argv", arguments))
            stack.enter_context(contextlib.redirect_stdout(io.StringIO()))
            stack.enter_context(contextlib.redirect_stderr(io.StringIO()))
            if backup_error:
                with self.assertRaises(SystemExit):
                    black.main()
                popen.assert_not_called()
            else:
                black.main()
                popen.assert_called_once()
                self.assertEqual(stopped.call_count, int(wait_effect is not None))
        return next((self.root / "out/macos/user").glob("run-*"))

    def test_normal_exit_records_written_files_and_original_backup(self):
        run_dir = self.launch()
        report = json.loads((run_dir / "saves-after.json").read_text())
        self.assertEqual(report["changes"]["changed"], ["UserData/45410083/slot/profile.dat"])
        self.assertEqual(json.loads((run_dir / "outcome.json").read_text()),
                         {"timed_stop": False, "exit_code": 0})

    def test_timeout_and_interrupt_stop_child_before_inventory(self):
        for effect, key in ((subprocess.TimeoutExpired("test", 10), "timed_stop"),
                            (KeyboardInterrupt(), "interrupted")):
            with self.subTest(key=key):
                run_dir = self.launch(wait_effect=effect)
                outcome = json.loads((run_dir / "outcome.json").read_text())
                self.assertTrue(outcome[key])
                self.assertFalse(outcome["forced_kill"])
                self.assertTrue((run_dir / "saves-after.json").is_file())
                # Keep the next subcase's run selection unambiguous.
                run_dir.rename(run_dir.with_name("checked-" + run_dir.name))

    def test_failed_backup_prevents_launch(self):
        self.launch(backup_error=ValueError("synthetic verification failure"))

    def test_dev_unlock_backup_uses_isolated_root_and_preserves_real_saves(self):
        original = self.profile.read_bytes()
        run_dir = self.launch(wait_effect=subprocess.TimeoutExpired("test", 10), dev=True)
        report = json.loads((run_dir / "saves-before.json").read_text())
        self.assertEqual(report["save_directory"], str(run_dir / "data/xboxrecomp"))
        self.assertFalse(report["snapshot"]["root_exists"])
        self.assertEqual(self.profile.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
