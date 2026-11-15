"""Path and checksum support shared by standalone hardware packages."""
from __future__ import annotations

import hashlib
import re
from pathlib import Path, PurePosixPath


def package_root(script: Path) -> Path | None:
    base = script.resolve().parents[2]
    if (base / "card").is_dir():
        return base
    if base.name == "package" and (base.parent / "release-snapshot").is_dir():
        return base.parent
    return None


def verify_package(root: Path) -> None:
    root = root.resolve()
    seen: set[str] = set()
    for line in (root / "PACKAGE_SHA256SUMS").read_text(encoding="utf-8").splitlines():
        digest, relative = line.split("  ", 1)
        name = PurePosixPath(relative)
        if not re.fullmatch(r"[0-9a-f]{64}", digest) or name.is_absolute() or ".." in name.parts or "\\" in relative or relative in seen:
            raise ValueError(f"Unsafe/duplicate package checksum entry: {relative}")
        path = root / relative
        if path.is_symlink() or not path.resolve().is_relative_to(root) or not path.is_file():
            raise ValueError(f"Package file is missing or outside package: {relative}")
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual != digest:
            raise ValueError(f"Package checksum mismatch: {relative}")
        seen.add(relative)
    if not seen:
        raise ValueError("Empty package checksum manifest")
    print(f"PASS PACKAGE-INTEGRITY: {len(seen)} files under {root}")
