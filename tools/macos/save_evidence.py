"""Read-only save snapshots and verified backups for manual BLACK validation."""
import hashlib
import json
from pathlib import Path
import tarfile


def sha256(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def inventory(root):
    root = Path(root)
    files, directories = {}, []
    if root.is_symlink() or (root.exists() and not root.is_dir()):
        raise ValueError(f"Save root is not a regular directory: {root}")
    if root.exists():
        for path in sorted(root.rglob("*")):
            name = path.relative_to(root).as_posix()
            if path.is_symlink():
                raise ValueError(f"Save backup refuses a symbolic link: {path}")
            if path.is_dir():
                directories.append(name)
            elif path.is_file():
                files[name] = {"size": path.stat().st_size, "sha256": sha256(path)}
            else:
                raise ValueError(f"Save backup refuses a special file: {path}")
    return {"root_exists": root.exists(), "directories": directories, "files": files}


def verify_archive(path, expected):
    files, directories = {}, []
    with tarfile.open(path, "r:gz") as archive:
        for member in archive:
            if member.name in files or member.name in directories:
                raise ValueError(f"Duplicate save backup member: {member.name}")
            if member.isdir():
                directories.append(member.name)
            elif member.isfile():
                with archive.extractfile(member) as stream:
                    files[member.name] = {"size": member.size,
                                          "sha256": hashlib.file_digest(stream, "sha256").hexdigest()}
            else:
                raise ValueError(f"Unexpected save backup member: {member.name}")
    if files != expected["files"] or sorted(directories) != expected["directories"]:
        raise ValueError("Save backup contents do not match their SHA-256 manifest")


def backup(root, run_dir):
    """Caller holds the launcher's run lock; no restore or save editing here."""
    root, run_dir = Path(root), Path(run_dir)
    before = inventory(root)
    archive_path = run_dir / "saves-before.tar.gz"
    # Raw cache partitions are sparse; compress without expanding a disk copy.
    with tarfile.open(archive_path, "x:gz", compresslevel=1) as archive:
        for name in sorted([*before["directories"], *before["files"]]):
            archive.add(root / name, arcname=name, recursive=False)
    verify_archive(archive_path, before)
    if inventory(root) != before:
        raise ValueError("Save files changed during backup; BLACK was not started")
    report = {"save_directory": str(root), "snapshot": before,
              "archive": archive_path.name, "archive_sha256": sha256(archive_path),
              "verified": True}
    (run_dir / "saves-before.json").write_text(json.dumps(report, indent=2) + "\n")
    return before


def record_after(root, run_dir, before):
    after = inventory(root)
    old, new = before["files"], after["files"]
    changes = {"added": sorted(new.keys() - old.keys()),
               "removed": sorted(old.keys() - new.keys()),
               "changed": sorted(name for name in old.keys() & new.keys() if old[name] != new[name])}
    report = {"save_directory": str(root), "snapshot": after, "changes": changes,
              "note": "File changes alone do not qualify a gameplay save/load."}
    (Path(run_dir) / "saves-after.json").write_text(json.dumps(report, indent=2) + "\n")
    return changes
