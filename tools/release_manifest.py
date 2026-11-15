#!/usr/bin/env python3
"""Writes SHA-256 manifests and versioned package manifests for the local release candidate (host tool)."""

from __future__ import annotations

import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
VERSION = "0.1.0-rc1"
ARTIFACTS = [
    ("x86_64-iso", "x86_64", "build/x86_64/ExperimentalOS.iso", "boot-image"),
    ("x86_64-uefi-app", "x86_64", "build/x86_64/esp/EFI/BOOT/BOOTX64.EFI", "native-build"),
    ("aarch64-pi5-image", "aarch64", "build/aarch64/ExperimentalOS-Pi5.img", "boot-image-structural"),
    ("aarch64-uefi-app", "aarch64", "build/aarch64/esp/EFI/BOOT/BOOTAA64.EFI", "native-build"),
    ("sdk", "any", "build/sdk/ExperimentalOS-SDK-0.1.0-rc1.tar.gz", "sdk"),
    ("devdeploy-manager-resources", "any", "build/sdk/RPi5-DevDeploy-Manager-Resources-0.1.0-rc1.tar.gz", "resources"),
]
ABSENT = {
    "build/x86_64/kernel-debug.elf": "not generated: the kernel is linked directly as a PE/COFF UEFI application",
    "build/aarch64/kernel-debug.elf": "not generated: the kernel is linked directly as a PE/COFF UEFI application",
    "development-deployment-manager-executable": "Not implemented: only contracts, sources and simulation exist",
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git(*args: str) -> str:
    return subprocess.run(["git", *args], cwd=ROOT, text=True, capture_output=True, check=True).stdout.strip()


def main() -> int:
    manifests = BUILD / "manifests"
    manifests.mkdir(parents=True, exist_ok=True)
    tracked = git("ls-files").splitlines()
    source_digest = hashlib.sha256()
    for name in sorted(tracked):
        path = ROOT / name
        if path.is_file():
            source_digest.update(name.encode() + b"\0" + hashlib.sha256(path.read_bytes()).digest())
    entries = []
    for ident, arch, rel, kind in ARTIFACTS:
        path = ROOT / rel
        if not path.is_file():
            raise SystemExit(f"missing artifact: {rel}")
        entries.append({"id": ident, "architecture": arch, "kind": kind, "path": rel,
                        "size": path.stat().st_size, "sha256": sha256(path), "executable_json": False})
    reports = sorted((BUILD / "test-results").glob("*"))
    release = {
        "name": "ExperimentalOS", "version": VERSION, "channel": "experimental-release-candidate",
        "source_commit": git("rev-parse", "HEAD"),
        "source_tree_dirty": bool(git("status", "--porcelain", "--untracked-files=no")),
        "tracked_sources_sha256": source_digest.hexdigest(),
        "artifacts": entries,
        "absent_artifacts": ABSENT,
        "test_reports": [{"path": p.relative_to(ROOT).as_posix(), "sha256": sha256(p)} for p in reports if p.is_file()],
        "hardware_status": "HARDWARE_NOT_VERIFIED",
        "raspberry_pi_5": "image structurally checked only; not boot-verified; not flashed",
        "signing": "none; no private keys or credentials are included or exported",
        "published": False,
    }
    (manifests / "release-manifest.json").write_text(json.dumps(release, indent=2, sort_keys=True) + "\n")
    package = {"schema": 1, "version": VERSION, "packages": [
        {"id": e["id"], "version": VERSION, "architecture": e["architecture"], "sha256": e["sha256"],
         "path": e["path"], "contains_private_keys": False} for e in entries]}
    (manifests / "package-manifest.json").write_text(json.dumps(package, indent=2, sort_keys=True) + "\n")
    lines = [f"{e['sha256']}  {e['path']}" for e in entries]
    lines += [f"{sha256(p)}  {p.relative_to(ROOT).as_posix()}" for p in reports if p.is_file()]
    for name in ("release-manifest.json", "package-manifest.json"):
        lines.append(f"{sha256(manifests / name)}  build/manifests/{name}")
    (manifests / "SHA256SUMS").write_text("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
