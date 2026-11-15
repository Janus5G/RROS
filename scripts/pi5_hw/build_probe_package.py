#!/usr/bin/env python3
"""Assemble the isolated Pi 5 probe test package (never touches release artifacts).

Inputs : build/pi5-probe/esp (probe build), build/pi5-boot-chain-cache/RPi5_UEFI_Release_v0.3.zip
         (download manually from the pinned URL in boot_chain_pins.json; hash-verified here),
         the newest logs/pi5_probe/*_qemu_probe_serial.log.
Output : build/pi5-probe-package/ with card/ (FAT32 root contents), docs, tools, PACKAGE_SHA256SUMS.
"""

from __future__ import annotations

import hashlib
import argparse
import json
import shutil
import sys
import zipfile
from pathlib import Path
from package_support import package_root, verify_package

ROOT = Path(__file__).resolve().parents[2]
PACKAGED_ROOT = package_root(Path(__file__))
PINS = json.loads((ROOT / "scripts/pi5_hw/boot_chain_pins.json").read_text())
ZIP = ROOT / "build/pi5-boot-chain-cache" / PINS["zip"]["name"]
PROBE = ROOT / "build/pi5-probe/esp/EFI/BOOT/BOOTAA64.EFI"
OUT = ROOT / "build/pi5-probe-package"
RELEASE = [("build/aarch64/ExperimentalOS-Pi5.img", None), ("build/x86_64/ExperimentalOS.iso", None)]
EXTRA = [
    "docs/PI5_HARDWARE_VALIDATION_PLAN.md", "docs/HARDWARE_VALIDATION_GUIDE.md",
    "scripts/pi5_hw/boot_chain_pins.json", "scripts/pi5_hw/build_probe_package.py",
    "scripts/pi5_hw/write_image_safe.ps1", "tests/pi5_hw_validation/capture_serial.py",
    "tests/pi5_hw_validation/evaluate_probe_log.py", "tests/pi5_hw_validation/RESULTS_TEMPLATE.md",
    "tests/pi5_hw_validation/evaluate_serial_log.py", "tests/pi5_hw_validation/selftest.py",
    "tests/pi5_hw_validation/requirements.txt",
    "scripts/pi5_hw/package_support.py",
]


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify", action="store_true", help="verify an existing package without repository inputs")
    args = parser.parse_args()
    if PACKAGED_ROOT or args.verify:
        verify_package(PACKAGED_ROOT or OUT)
        return 0
    if not ZIP.is_file():
        print(f"BLOCKED: firmware zip missing: {ZIP}\n  download {PINS['url']} (owner-approved) and retry")
        return 3
    zdata = ZIP.read_bytes()
    if len(zdata) != PINS["zip"]["size"] or sha(zdata) != PINS["zip"]["sha256"]:
        print("FAIL: firmware zip does not match the pinned size/SHA-256")
        return 1
    if not PROBE.is_file():
        print("FAIL: probe not built (run scripts/build-pi5-probe.sh)")
        return 1
    logs = sorted((ROOT / "logs/pi5_probe").glob("*_qemu_probe_serial.log"))
    if not logs:
        print("FAIL: no QEMU probe log; run scripts/run-pi5-probe-qemu.sh first")
        return 1
    if OUT.exists():
        shutil.rmtree(OUT)
    card = OUT / "card"
    with zipfile.ZipFile(ZIP) as z:
        for name, meta in PINS["files"].items():
            data = z.read(name)
            if len(data) != meta["size"] or sha(data) != meta["sha256"]:
                print(f"FAIL: {name} in zip does not match its pinned hash")
                return 1
            (card / name).parent.mkdir(parents=True, exist_ok=True)
            (card / name).write_bytes(data)
    (card / "EFI/BOOT").mkdir(parents=True)
    shutil.copyfile(PROBE, card / "EFI/BOOT/BOOTAA64.EFI")
    for rel in EXTRA:
        dest = OUT / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / rel, dest)
    shutil.copyfile(logs[-1], OUT / "qemu_probe_serial.log")
    manifest = ROOT / "build/manifests/SHA256SUMS"
    recorded = {}
    for line in manifest.read_text().splitlines():
        if line.strip():
            d, r = line.split(None, 1)
            recorded[r.strip().lstrip("*")] = d
    ref = {}
    for rel, _ in RELEASE:
        p = ROOT / rel
        ref[rel] = {"sha256": sha(p.read_bytes()), "manifest": recorded.get(rel)} if p.is_file() else None
    (OUT / "release_reference.json").write_text(json.dumps(ref, indent=2) + "\n")
    (OUT / "STATUS.md").write_text(
        "# Pi 5 probe package status\n\n"
        "- Probe build + QEMU/UEFI run: SOFTWARE_PASS (virtual platform only)\n"
        "- Raspberry Pi 5 boot with this card: HARDWARE_NOT_VERIFIED\n"
        "- Physical test execution: BLOCKED_BY_PHYSICAL_HARDWARE (owner authorization required)\n"
        "- Release image/ISO: untouched; NOT a Pi 5 boot image (QEMU console build)\n"
        "\n## Standalone commands (run from this package root)\n"
        "\n`python scripts/pi5_hw/build_probe_package.py --verify`\n"
        "\n`python tests/pi5_hw_validation/selftest.py`\n"
        "\n`python tests/pi5_hw_validation/evaluate_probe_log.py --mode qemu qemu_probe_serial.log`\n"
        "\nFor a physical capture use `--mode pi5 YOUR_CAPTURE.serial.log`; the legacy "
        "`evaluate_serial_log.py` is for old G02/G12 logs, not probe logs.\n"
    )
    lines = [f"{sha(p.read_bytes())}  {p.relative_to(OUT).as_posix()}"
             for p in sorted(OUT.rglob("*")) if p.is_file() and p.name != "PACKAGE_SHA256SUMS"]
    (OUT / "PACKAGE_SHA256SUMS").write_text("\n".join(lines) + "\n")
    print(f"package written to {OUT.relative_to(ROOT)} ({len(lines)} files)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
