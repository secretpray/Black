#!/usr/bin/env python3
"""Inspect captured game PCM without equating nonzero samples with correct sound."""
import argparse
import array
import json
import math
from pathlib import Path
import sys
import wave


def repeated_packets(path, frames=256):
    """Exact varying PCM repeats identify buffer replay, not perceptual echo.

    Ignore silence and DC packets (including a constant stereo pair), whose
    legitimate repetition says nothing about audio-buffer ownership.
    """
    packet_bytes = frames * 4
    previous = None
    index = repeats = longest = run = 0
    spans = []
    last_repeat = -2
    stored_span = False
    with path.open("rb") as stream:
        while data := stream.read(packet_bytes):
            if len(data) != packet_bytes:
                break
            varying = data != data[:4] * frames
            if data == previous and varying:
                repeats += 1
                run += 1
                longest = max(longest, run)
                if index == last_repeat + 1:
                    if stored_span:
                        spans[-1]["repeated_packets"] += 1
                else:
                    stored_span = len(spans) < 30
                    if stored_span:
                        spans.append({"at_seconds": round(index * frames / 48000, 6), "repeated_packets": 1})
                last_repeat = index
            else:
                run = 1
            previous = data
            index += 1
    return {"packet_frames": frames, "complete_packets": index,
            "repeated_varying_packets": repeats,
            "replayed_seconds": repeats * frames / 48000,
            "longest_varying_run_packets": longest, "first_spans": spans,
            "scope": "Exact adjacent varying PCM packets; excludes silence/DC. Does not measure reverb or prove every repeat is a bug."}


def analyze(path):
    seconds = []
    with path.open("rb") as stream:
        while data := stream.read(48000 * 4):
            if len(data) % 4:
                raise ValueError("Partial stereo frame in PCM capture")
            samples = array.array("h", data)
            if sys.byteorder != "little":
                samples.byteswap()
            count = len(samples)
            energy = sum(int(s) * int(s) for s in samples)
            peak = max(map(abs, samples), default=0)
            rms = math.sqrt(energy / max(count, 1))
            seconds.append({"index": len(seconds), "frames": count // 2, "peak": peak,
                            "rms_dbfs": round(20 * math.log10(rms / 32768), 2) if rms else None,
                            "nonzero_samples": sum(s != 0 for s in samples),
                            "clipped_samples": sum(s in (-32768, 32767) for s in samples)})
    result = {"format": "s16le stereo 48000 Hz", "frames": sum(s["frames"] for s in seconds),
              "duration_seconds": sum(s["frames"] for s in seconds) / 48000,
              "peak": max((s["peak"] for s in seconds), default=0),
              "nonzero_samples": sum(s["nonzero_samples"] for s in seconds),
              "clipped_samples": sum(s["clipped_samples"] for s in seconds), "seconds": seconds,
              "scope": "Accepted PCM before SDL playback. Nonzero data does not prove correct game audio or hardware output."}
    result["packet_repeats"] = repeated_packets(path)
    return result


def analyze_float_mix(path):
    seconds = []
    with path.open("rb") as stream:
        while data := stream.read(48000 * 8):
            if len(data) % 8:
                raise ValueError("Partial stereo frame in floating-point capture")
            samples = array.array("f", data)
            if sys.byteorder != "little":
                samples.byteswap()
            finite = [s for s in samples if math.isfinite(s)]
            seconds.append({"index": len(seconds), "frames": len(samples) // 2,
                            "peak": max(map(abs, finite), default=0),
                            "over_full_scale": sum(abs(s) >= 1 for s in finite),
                            "nonfinite": len(samples) - len(finite)})
    return {"format": "f32le stereo 48000 Hz before output protection",
            "frames": sum(s["frames"] for s in seconds),
            "peak": max((s["peak"] for s in seconds), default=0),
            "over_full_scale": sum(s["over_full_scale"] for s in seconds),
            "nonfinite": sum(s["nonfinite"] for s in seconds), "seconds": seconds}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pcm", type=Path)
    parser.add_argument("--wav", action="store_true", help="Also wrap the raw capture in a WAV for local listening")
    parser.add_argument("--float-mix", type=Path, help="Also analyze the pre-conversion floating-point stereo mix")
    args = parser.parse_args()
    result = analyze(args.pcm)
    target = args.pcm.with_suffix(".json")
    target.write_text(json.dumps(result, indent=2) + "\n")
    if args.float_mix:
        raw = analyze_float_mix(args.float_mix)
        args.float_mix.with_suffix(".json").write_text(json.dumps(raw, indent=2) + "\n")
        print(json.dumps({k: v for k, v in raw.items() if k != "seconds"}, indent=2))
    if args.wav:
        with wave.open(str(args.pcm.with_suffix(".wav")), "wb") as wav:
            wav.setnchannels(2)
            wav.setsampwidth(2)
            wav.setframerate(48000)
            with args.pcm.open("rb") as pcm:
                while block := pcm.read(192000):
                    wav.writeframesraw(block)
    print(json.dumps({k: v for k, v in result.items() if k != "seconds"}, indent=2))
    print(target)


if __name__ == "__main__":
    main()
