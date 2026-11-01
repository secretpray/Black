#!/usr/bin/env python3
"""Summarize a bounded BLACK --profile-frames run, without image capture."""
import argparse
import csv
import json
from pathlib import Path
import re
import statistics


def distribution(values):
    values = sorted(values)
    if not values:
        return {"count": 0}
    def percentile(p):
        return values[round((len(values) - 1) * p)]
    return {"count": len(values), "median_ms": statistics.median(values),
            "p95_ms": percentile(.95), "p99_ms": percentile(.99), "max_ms": values[-1],
            "over_40ms": sum(v > 40 for v in values), "over_50ms": sum(v > 50 for v in values)}


def load(path):
    if not path.exists():
        return []
    rows = []
    with path.open() as stream:
        for row in csv.DictReader(stream):
            try:
                rows.append({k: int(v) for k, v in row.items()})
            except (ValueError, TypeError):
                pass  # a live buffered file can end in an incomplete row
    return rows


def analyze(run, settle=5):
    states = []
    for line in (run / "game.log").read_text(errors="replace").splitlines():
        if line.startswith("[STATE]"):
            fields = dict(re.findall(r"(\w+)=([^ ]+)", line))
            if fields.get("eye", "0.0000,0.0000,0.0000") != "0.0000,0.0000,0.0000":
                states.append(fields)
    if not states:
        raise ValueError("No initialized 3D scene in STATE log")
    begin = int(states[0]["wall_ms"]) * 1000000 + int(settle * 1e9)
    end = int(states[-1]["wall_ms"]) * 1000000
    channels = {}
    for path in (run / "frame-trace").glob("*.csv"):
        channels[path.stem] = [r for r in load(path) if begin <= r["start_ns"] <= end]
    report = {"scope": "Initialized first scene only, no screenshot capture; timings include profiling overhead.",
              "begin_ns": begin, "end_ns": end, "duration_s": (end - begin) / 1e9,
              "stages": {}}
    for channel, rows in channels.items():
        rows.sort(key=lambda r: r["start_ns"])
        item = {"duration": distribution([(r["end_ns"] - r["start_ns"]) / 1e6 for r in rows]),
                "start_interval": distribution([(b["start_ns"] - a["start_ns"]) / 1e6 for a, b in zip(rows, rows[1:])])}
        if channel == "world":
            item["requested_sleep"] = distribution([r["a"] / 1e6 for r in rows])
        if channel == "source":
            seqs = [r["a"] for r in rows]
            item["backwards_snapshot_ids"] = sum(b < a for a, b in zip(seqs, seqs[1:]))
            item["distinct_snapshots"] = len(set(seqs))
            counts = []
            for seq in dict.fromkeys(seqs):
                counts.append(seqs.count(seq))
            item["submissions_per_snapshot"] = {str(n): counts.count(n) for n in sorted(set(counts))}
            item["note"] = "Completed GPU snapshot chosen as Metal source; sequence order is measured before drawable presentation."
        if channel == "display":
            shown = sorted(r["a"] for r in rows if r["a"])
            item["presented_interval"] = distribution([(b - a) / 1e6 for a, b in zip(shown, shown[1:])])
            by_display = sorted(rows, key=lambda r: r["a"])
            cadence = []
            previous, repeats = None, 0
            for row in by_display:
                if row["b"] == previous:
                    repeats += 1
                else:
                    if previous is not None:
                        cadence.append(repeats)
                    previous, repeats = row["b"], 1
            item["submissions_per_observed_flip"] = {str(n): cadence.count(n) for n in sorted(set(cadence))}
            item["note"] = "Drawable presentedTime; observed flip IDs are CPU-side, not pixel identity or photodiode scanout."
        report["stages"][channel] = item
    # Attribute slow flip-to-flip gaps to measured CPU-side portions. Durations
    # of nested readbacks/downloads must not be added together.
    flips = channels.get("flip", [])
    slow = []
    for previous, current in zip(flips, flips[1:]):
        gap = (current["end_ns"] - previous["end_ns"]) / 1e6
        if gap <= 40:
            continue
        row = {"frame": current["frame"], "gap_ms": gap,
               "flip_ms": (current["end_ns"] - current["start_ns"]) / 1e6,
               "draws": current["a"]}
        for category in ("download", "capture", "readback", "shader", "world"):
            row[category + "_ms"] = sum((min(r["end_ns"], current["end_ns"]) - max(r["start_ns"], previous["end_ns"])) / 1e6
                for r in channels.get(category, []) if r["end_ns"] > previous["end_ns"] and r["start_ns"] < current["end_ns"])
        slow.append(row)
    report["slow_flips"] = sorted(slow, key=lambda r: r["gap_ms"], reverse=True)[:30]
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path)
    parser.add_argument("--settle", type=float, default=5)
    args = parser.parse_args()
    result = analyze(args.run, args.settle)
    output = args.run / "frame-analysis.json"
    output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    print(output)
