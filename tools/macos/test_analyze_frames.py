"""Trace attribution uses synthetic time values, never game assets."""
import csv
import tempfile
from pathlib import Path
import unittest
from analyze_frames import analyze, distribution


class FrameAnalysis(unittest.TestCase):
    def test_intervals_and_empty_channel(self):
        self.assertEqual(distribution([]), {"count": 0})
        result = distribution([16, 33, 50, 100])
        self.assertEqual(result["over_50ms"], 1)
        self.assertEqual(result["p99_ms"], 100)

    def test_stage_attribution_and_3d_scope(self):
        with tempfile.TemporaryDirectory() as directory:
            run = Path(directory)
            trace = run / "frame-trace"
            trace.mkdir()
            (run / "game.log").write_text("[STATE] eye=0.0000,0.0000,0.0000 wall_ms=500\n"
                "[STATE] eye=1.0000,2.0000,3.0000 wall_ms=1000\n"
                "[STATE] eye=1.0000,2.0000,3.0000 wall_ms=2000\n")
            def write(name, rows):
                with (trace / (name + ".csv")).open("w") as stream:
                    w = csv.writer(stream)
                    w.writerow(["frame", "start_ns", "end_ns", "a", "b"])
                    w.writerows(rows)
            write("flip", [(1, 1100000000, 1101000000, 10, 0),
                           (2, 1140000000, 1161000000, 20, 0)])
            write("download", [(2, 1140000000, 1160000000, 0, 0)])
            write("source", [(1, 1100000000, 1100000000, 5, 7),
                             (2, 1110000000, 1110000000, 5, 7),
                             (3, 1140000000, 1140000000, 6, 8),
                             (4, 1150000000, 1150000000, 4, 9)])
            result = analyze(run, settle=0)
            self.assertEqual(result["duration_s"], 1)
            self.assertEqual(result["slow_flips"][0]["gap_ms"], 60)
            self.assertEqual(result["slow_flips"][0]["download_ms"], 20)
            self.assertEqual(result["stages"]["source"]["backwards_snapshot_ids"], 1)
            self.assertEqual(result["stages"]["source"]["submissions_per_snapshot"], {"1": 2, "2": 1})


if __name__ == "__main__":
    unittest.main()
