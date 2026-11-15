#!/usr/bin/env python3
"""Build the Pi 5 hardware-validation package without modifying any release artifact.

Read-only inputs : build/aarch64/ExperimentalOS-Pi5.img, build/manifests/SHA256SUMS,
                   build/aarch64/esp/EFI/BOOT/BOOTAA64.EFI (current, possibly a test build).
New outputs only : build/pi5-hw-validation/ (refuses to overwrite an existing package unless --force,
                   and never writes outside that directory).
Investigates the ESP/manifest drift and writes DRIFT_REPORT.md/json plus PACKAGE_SHA256SUMS.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path
from package_support import package_root, verify_package

SCRIPT_ROOT = Path(__file__).resolve().parents[2]
PACKAGED_ROOT = package_root(Path(__file__))
ROOT = PACKAGED_ROOT or SCRIPT_ROOT
sys.path.insert(0, str(SCRIPT_ROOT / "tools"))
import checkimage  # noqa: E402  (read-only structural parser)

IMAGE = ROOT / "build/aarch64/ExperimentalOS-Pi5.img"
MANIFEST = ROOT / "build/manifests/SHA256SUMS"
CURRENT_APP = ROOT / "build/aarch64/esp/EFI/BOOT/BOOTAA64.EFI"
OUT = ROOT / "build/pi5-hw-validation"
BOOT_PATH = "EFI/BOOT/BOOTAA64.EFI"
# Strings that exist only in the G12 test build of the boot application.
TEST_BUILD_MARKERS = [b"G12 INPUT READY", b"G12 FDT BOUNDS DISCOVERY PASS", b"G12 HARDWARE DRIVERS NOT VERIFIED"]
QEMU_UART_BASE = (0x09000000).to_bytes(4, "little")

PACKAGE_FILES = [
    "docs/PI5_HARDWARE_VALIDATION_PLAN.md",
    "docs/HARDWARE_VALIDATION_GUIDE.md",
    "scripts/pi5_hw/build_package.py",
    "scripts/pi5_hw/package_support.py",
    "tools/checkimage.py",
    "scripts/pi5_hw/write_image_safe.ps1",
    "tests/pi5_hw_validation/capture_serial.py",
    "tests/pi5_hw_validation/evaluate_serial_log.py",
    "tests/pi5_hw_validation/selftest.py",
    "tests/pi5_hw_validation/RESULTS_TEMPLATE.md",
]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def stamp(path: Path) -> str:
    return dt.datetime.fromtimestamp(path.stat().st_mtime).astimezone().isoformat(timespec="seconds")


def manifest_hashes() -> dict[str, str]:
    result = {}
    for line in MANIFEST.read_text().splitlines():
        if line.strip():
            digest, rel = line.split(None, 1)
            result[rel.strip().lstrip("*")] = digest
    return result


def git(*args: str) -> str:
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True).stdout.strip()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--force", action="store_true", help="replace an existing build/pi5-hw-validation")
    parser.add_argument("--verify", action="store_true", help="verify a copied standalone package read-only")
    args = parser.parse_args()
    if PACKAGED_ROOT or args.verify:
        if args.force:
            parser.error("--force is not allowed for standalone package verification")
        package = PACKAGED_ROOT or OUT
        verify_package(package)
        snapshot = package / "release-snapshot"
        checkimage.check_disk(snapshot / IMAGE.name, BOOT_PATH, "aarch64",
                              snapshot / "BOOTAA64.EFI.from-image")
        print("PASS STANDALONE-IMAGE: GPT/FAT/PE and embedded boot application (not hardware)")
        return 0

    if OUT.exists():
        if not args.force:
            print(f"refusing to overwrite {OUT}; pass --force to rebuild the package directory")
            return 2
        shutil.rmtree(OUT)
    (OUT / "release-snapshot").mkdir(parents=True)

    recorded = manifest_hashes()
    image_hash = sha256(IMAGE)
    if recorded.get("build/aarch64/ExperimentalOS-Pi5.img") != image_hash:
        print("FAIL: release image does not match the release manifest; not building a package")
        return 1

    disk = IMAGE.read_bytes()
    start, end = checkimage.check_gpt(disk)
    embedded = checkimage.Fat(disk[start * 512:(end + 1) * 512]).read(BOOT_PATH)
    checkimage.check_pe(embedded, "aarch64")
    embedded_hash = hashlib.sha256(embedded).hexdigest()
    release_app_rel = "build/aarch64/esp/EFI/BOOT/BOOTAA64.EFI"
    if embedded_hash != recorded.get(release_app_rel):
        print("FAIL: boot application embedded in the image does not match the release manifest")
        return 1

    snap = OUT / "release-snapshot"
    shutil.copyfile(IMAGE, snap / IMAGE.name)
    (snap / "BOOTAA64.EFI.from-image").write_bytes(embedded)
    if sha256(snap / IMAGE.name) != image_hash:
        print("FAIL: snapshot copy of the image differs from the original")
        return 1

    current = CURRENT_APP.read_bytes()
    current_hash = hashlib.sha256(current).hexdigest()
    evidence = {
        "release_image": {"path": "build/aarch64/ExperimentalOS-Pi5.img", "sha256": image_hash,
                          "mtime": stamp(IMAGE), "size": IMAGE.stat().st_size},
        "release_manifest": {"path": "build/manifests/SHA256SUMS", "mtime": stamp(MANIFEST)},
        "manifest_recorded_boot_app_sha256": recorded.get(release_app_rel),
        "image_embedded_boot_app": {"sha256": embedded_hash, "size": len(embedded),
                                    "matches_manifest": embedded_hash == recorded.get(release_app_rel),
                                    "contains_test_build_markers": {m.decode(): m in embedded for m in TEST_BUILD_MARKERS},
                                    "contains_qemu_virt_pl011_address_0x09000000": QEMU_UART_BASE in embedded},
        "current_esp_boot_app": {"path": release_app_rel, "sha256": current_hash, "size": len(current),
                                 "mtime": stamp(CURRENT_APP), "matches_manifest": current_hash == recorded.get(release_app_rel),
                                 "contains_test_build_markers": {m.decode(): m in current for m in TEST_BUILD_MARKERS}},
        "git_head": git("rev-parse", "HEAD"),
    }
    cause = ("build/aarch64/esp is a single shared build-output path. scripts/build-aarch64.sh writes it for every "
             "gate run and compiles the G12 test build when EXPERIMENTALOS_G12_TEST=1; scripts/build-release.sh unsets "
             "all test flags and assembles the release image from that path. Gate runs after the release (including "
             "G12) therefore replaced the release boot application on disk. The release image itself and the "
             "boot application embedded in it were not changed and still match the manifest.")
    evidence["cause"] = cause
    (OUT / "drift_evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")

    drift = [
        "# ESP / release-manifest drift investigation", "",
        f"- Git HEAD: `{evidence['git_head']}`",
        f"- Release image `{image_hash}` ({evidence['release_image']['mtime']}): matches manifest.",
        f"- Boot app embedded in image `{embedded_hash}`: matches manifest = {evidence['image_embedded_boot_app']['matches_manifest']}.",
        f"- Current `build/aarch64/esp/EFI/BOOT/BOOTAA64.EFI` `{current_hash}` ({evidence['current_esp_boot_app']['mtime']}): matches manifest = {evidence['current_esp_boot_app']['matches_manifest']}.",
        f"- Test-build markers in embedded release app: {evidence['image_embedded_boot_app']['contains_test_build_markers']}",
        f"- Test-build markers in current ESP app: {evidence['current_esp_boot_app']['contains_test_build_markers']}",
        f"- Embedded release app contains the QEMU virt PL011 base address constant 0x09000000: {evidence['image_embedded_boot_app']['contains_qemu_virt_pl011_address_0x09000000']} (source: boot/aarch64/efi_main.c `uart_write`).",
        "", "## Cause", "", cause, "",
        "## Handling", "",
        "- No original file was changed. Hardware validation uses the byte-identical copy in `release-snapshot/`.",
        "- Do NOT run `scripts/build-release.sh`: it starts with `rm -rf build/aarch64 build/x86_64 ...` and would delete the release image.",
        "- Do NOT copy `build/aarch64/esp` to a card: it is a G12 test build that waits for a QMP-injected key.",
        "- The release source revision is not exactly reproducible: the release manifest records `source_tree_dirty: true` for its SDK packages.",
        "- Durable fix (needs owner authorization, outside this package): give build-release a private output directory.",
        "", "## Standalone verification", "",
        "From this package root: `python package/scripts/pi5_hw/build_package.py --verify`.",
        "The copied script defaults to read-only package/image verification; it never searches for the original checkout.",
        "`package/tools/checkimage.py` is included. No build tools, firmware or original repository are required.",
    ]
    (OUT / "DRIFT_REPORT.md").write_text("\n".join(drift) + "\n")

    package_dir = OUT / "package"
    for rel in PACKAGE_FILES:
        target = package_dir / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / rel, target)
    lines = []
    for path in sorted(OUT.rglob("*")):
        if path.is_file() and path.name != "PACKAGE_SHA256SUMS":
            lines.append(f"{sha256(path)}  {path.relative_to(OUT).as_posix()}")
    (OUT / "PACKAGE_SHA256SUMS").write_text("\n".join(lines) + "\n")
    print(f"package written to {OUT.relative_to(ROOT)} ({len(lines)} files)")
    print(f"embedded boot app matches manifest: {evidence['image_embedded_boot_app']['matches_manifest']}")
    print(f"current ESP matches manifest: {evidence['current_esp_boot_app']['matches_manifest']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
