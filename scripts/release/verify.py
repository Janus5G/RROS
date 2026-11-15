#!/usr/bin/env python3
"""Fail-closed verification of downloaded release files and native payload provenance."""
from __future__ import annotations

import argparse
import hashlib
import json
import tarfile
from pathlib import Path, PurePosixPath


def sha(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def payloads(report: dict) -> dict:
    matches = [r for r in report["results"] if r["name"] == "release-reproducibility"]
    if len(matches) != 1 or matches[0]["status"] != "PASS":
        raise ValueError("Missing reproducible native-payload evidence")
    return matches[0]["sha256"]


def verify(directory: Path, expected_commit: str | None = None,
           rebuilt: Path | None = None) -> None:
    sums: dict[str, str] = {}
    for line in (directory / "SHA256SUMS").read_text(encoding="utf-8").splitlines():
        digest, name = line.split("  ", 1)
        if Path(name).name != name or name in sums or len(digest) != 64:
            raise ValueError(f"Unsafe/duplicate checksum entry: {name}")
        if sha(directory / name) != digest:
            raise ValueError(f"Checksum mismatch: {name}")
        sums[name] = digest
    manifest = json.loads((directory / "release-manifest.json").read_text(encoding="utf-8"))
    if manifest["software_status"] != "PASS":
        raise ValueError("Candidate is not a full software PASS")
    if manifest["hardware_status"] != "HARDWARE_NOT_VERIFIED":
        raise ValueError("Unsupported physical hardware claim")
    if expected_commit and manifest["source_commit"] != expected_commit:
        raise ValueError("Published candidate does not match the release event commit")
    if expected_commit and manifest["source_tree_dirty"]:
        raise ValueError("Published candidate was built from an uncommitted snapshot")
    if "release-manifest.json" not in sums:
        raise ValueError("Manifest is not covered by checksums")
    entries = manifest["artifacts"]
    native = payloads(manifest)
    bindings = {
        "ExperimentalOS.iso": "build/x86_64/ExperimentalOS.iso",
        "ExperimentalOS-Pi5.img": "build/aarch64/ExperimentalOS-Pi5.img",
        "EFI/BOOT/BOOTX64.EFI": "build/x86_64/esp/EFI/BOOT/BOOTX64.EFI",
        "EFI/BOOT/BOOTAA64.EFI": "build/aarch64/esp/EFI/BOOT/BOOTAA64.EFI",
    }
    if len(entries) != 7:
        raise ValueError("Expected seven complete versioned archives")
    if set(sums) != {"release-manifest.json", *(entry["name"] for entry in entries)}:
        raise ValueError("Checksum file does not cover exactly the distributed artifacts")
    for entry in entries:
        name = entry["name"]
        if Path(name).name != name or sums.get(name) != entry["sha256"]:
            raise ValueError(f"Artifact manifest mismatch: {name}")
        path = directory / name
        if path.stat().st_size != entry["size"]:
            raise ValueError(f"Artifact size mismatch: {name}")
        if name.endswith("-SDK.tar.gz") and sha(path) != native["build/sdk/ExperimentalOS-SDK-0.1.0-rc1.tar.gz"]:
            raise ValueError("SDK does not match the reproducible build")
        if name.endswith("-DevDeploy-resources.tar.gz") and sha(path) != native["build/sdk/RPi5-DevDeploy-Manager-Resources-0.1.0-rc1.tar.gz"]:
            raise ValueError("Deployment resources do not match the reproducible build")
        with tarfile.open(path) as tar:
            observed: set[str] = set()
            for member in tar.getmembers():
                relative = PurePosixPath(member.name)
                if relative.is_absolute() or ".." in relative.parts or not (member.isfile() or member.isdir()):
                    raise ValueError(f"Unsafe archive member: {member.name}")
                if name.endswith("-Pi5-probe.tar.gz") and relative.name in {
                    "RPI_EFI.fd", "config.txt", "bcm2712-rpi-5-b.dtb"
                }:
                    raise ValueError("Third-party firmware unexpectedly bundled")
                if name.endswith("-source.tar.gz") and relative.name == "OSBuild.pdf":
                    raise ValueError("Uncleared reference PDF unexpectedly bundled")
                if name.endswith(("-x86_64.tar.gz", "-aarch64-qemu.tar.gz")) and member.name in bindings:
                    stream = tar.extractfile(member)
                    if stream is None:
                        raise ValueError(f"Missing payload bytes: {member.name}")
                    digest = hashlib.sha256()
                    with stream:
                        for chunk in iter(lambda: stream.read(1 << 20), b""):
                            digest.update(chunk)
                    if digest.hexdigest() != native[bindings[member.name]]:
                        raise ValueError(f"Archive payload does not match build evidence: {member.name}")
                    observed.add(member.name)
            expected = {"ExperimentalOS.iso", "EFI/BOOT/BOOTX64.EFI"} if name.endswith("-x86_64.tar.gz") else (
                {"ExperimentalOS-Pi5.img", "EFI/BOOT/BOOTAA64.EFI"} if name.endswith("-aarch64-qemu.tar.gz") else set())
            if observed != expected:
                raise ValueError("Incomplete native payload archive")
    if rebuilt:
        fresh = json.loads(rebuilt.read_text(encoding="utf-8"))
        if payloads(manifest) != payloads(fresh):
            raise ValueError("Published native payloads differ from the fresh event build")
        if manifest["source_snapshot_sha256"] != fresh["source_snapshot_sha256"]:
            raise ValueError("Published source snapshot differs from the fresh event build")
    print("PASS release checksums, archive safety, complete manifest and evidence boundaries")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument("--expected-commit")
    parser.add_argument("--rebuilt-manifest", type=Path)
    args = parser.parse_args()
    verify(args.directory, args.expected_commit, args.rebuilt_manifest)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
