#!/usr/bin/env python3
"""Inspect a BLACK profile without changing it, its metadata or signature.

This is a format/signature diagnostic, not a save importer or gameplay validator.
The zero-key candidate follows this checkout's pinned native-C kernel exports.
"""
import argparse
import hashlib
import hmac
import json
import os
from pathlib import Path
import stat
import struct

PROFILE_SIZE = 0x1800
SIGNATURE_OFFSET = 0x17EC


def read_regular(path, limit):
    """Bound reads and refuse symlinks/special files; detect concurrent edits."""
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        before = os.fstat(fd)
        if not stat.S_ISREG(before.st_mode) or before.st_size > limit:
            raise ValueError(f"Not a bounded regular file: {path}")
        with os.fdopen(fd, "rb", closefd=False) as stream:
            data = stream.read(limit + 1)
            after = os.fstat(fd)
    finally:
        os.close(fd)
    fields = ("st_size", "st_mtime_ns", "st_ctime_ns")
    if len(data) != before.st_size or any(getattr(before, k) != getattr(after, k) for k in fields):
        raise ValueError(f"File changed while reading: {path}")
    return data


def folder_name(name):
    """XAPI sub_000237CB: exact UTF-16 code units, no case/Unicode folding."""
    if not name or "\0" in name:
        raise ValueError("Save name must be nonempty and contain no NUL")
    value = 0
    for (unit,) in struct.iter_unpack("<H", name.encode("utf-16le")):
        value = (value * 0x10000 + unit) % 0xFFFFFFFFFFC5
    return f"{value:012X}"


def inspect(profile):
    profile = Path(profile)
    data = read_regular(profile, PROFILE_SIZE)
    if len(data) != PROFILE_SIZE:
        raise ValueError("BLACK profile must be exactly 0x1800 bytes")
    inner = hmac.digest(bytes(16), data[:SIGNATURE_OFFSET], "sha1")
    candidate = hmac.digest(bytes(16), inner, "sha1")
    legacy_name = profile.name.encode("utf-8").decode("latin-1")
    report = {
        "size": len(data), "sha256": hashlib.sha256(data).hexdigest(),
        "double_zero_hmac_matches": hmac.compare_digest(data[SIGNATURE_OFFSET:], candidate),
        "filename": profile.name,
        "folder_from_unicode_filename": folder_name(profile.name),
        "folder_from_legacy_utf8_bytes": folder_name(legacy_name),
        "folder_matches_legacy_utf8_bytes": profile.parent.name.upper() == folder_name(legacy_name),
        "note": "Signature match does not validate game progress. A mismatch can mean different console keys or damaged data. No files changed.",
    }
    metadata = profile.parent / "SaveMeta.xbx"
    if metadata.exists() or metadata.is_symlink():
        raw = read_regular(metadata, 65536)
        text = raw.decode("utf-16le").removeprefix("\ufeff")
        # Legacy ANSI widening can put U+0085 inside an UTF-8-derived name;
        # str.splitlines() would incorrectly treat that byte as a separator.
        names = [line[5:] for line in text.replace("\r", "\n").split("\n") if line.startswith("Name=")]
        if len(names) != 1:
            raise ValueError("SaveMeta.xbx must contain exactly one Name= line")
        report.update(metadata_name=names[0], folder_from_metadata=folder_name(names[0]),
                      folder_matches_metadata=profile.parent.name.upper() == folder_name(names[0]),
                      metadata_name_is_legacy_utf8_bytes=names[0] == legacy_name)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", type=Path, help="Explicit profile file; read only")
    args = parser.parse_args()
    try:
        print(json.dumps(inspect(args.profile), ensure_ascii=True, indent=2))
    except (OSError, ValueError, UnicodeError) as error:
        parser.exit(1, f"BLACK profile inspection: {error}\n")


if __name__ == "__main__":
    main()
