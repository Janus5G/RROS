#!/usr/bin/env python3
"""Build and boot the x86-64 UEFI image in the actual QEMU guest."""

from __future__ import annotations

import os
import shlex
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
IMAGE = ROOT / "build" / "dev" / "x86_64" / "esp" / "EFI" / "BOOT" / "BOOTX64.EFI"
SERIAL_LOG = ROOT / "build" / "x86_64" / "qemu-serial.log"


def check(condition: bool, test_id: str, message: str) -> None:
    if not condition:
        raise AssertionError(f"FAIL {test_id}: {message}")
    print(f"PASS {test_id}: {message}")


def run_script(script: str) -> str:
    print(f"COMMAND: sh {script} (inside WSL)")
    if os.name == "nt":
        root = subprocess.run(
            ["wsl", "--exec", "wslpath", "-a", str(ROOT)],
            text=True, encoding="utf-8", capture_output=True, check=True,
        ).stdout.strip()
        command = f"cd {shlex.quote(root)} && sh {shlex.quote(script)}"
        result = subprocess.run(
            ["wsl", "--exec", "sh", "-lc", command],
            text=True, encoding="utf-8", stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            check=False,
        )
    else:
        result = subprocess.run(
            ["sh", str(ROOT / script)], cwd=ROOT, text=True, encoding="utf-8",
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False,
        )
    if result.returncode:
        raise RuntimeError(f"{script} failed ({result.returncode}):\n{result.stdout}")
    return result.stdout.strip()


def main() -> int:
    source = ROOT / "boot" / "x86_64" / "efi_main.c"
    check(source.is_file(), "G01-SOURCE", "freestanding UEFI entry source exists")

    build_output = run_script("scripts/build-x86_64.sh")
    print(build_output)
    check(IMAGE.is_file() and IMAGE.stat().st_size > 512,
          "G01-IMAGE", "UEFI boot image was built")
    data = IMAGE.read_bytes()
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    check(data[:2] == b"MZ" and data[pe_offset:pe_offset + 4] == b"PE\0\0",
          "G01-PE", "boot image has valid PE/COFF headers")
    check(struct.unpack_from("<H", data, pe_offset + 4)[0] == 0x8664,
          "G01-MACHINE", "boot image targets x86-64")
    check(struct.unpack_from("<H", data, pe_offset + 24 + 68)[0] == 10,
          "G01-EFI-SUBSYSTEM", "boot image is a UEFI application")

    qemu_output = run_script("scripts/run-qemu-x86_64.sh")
    print(qemu_output)
    serial = SERIAL_LOG.read_text(encoding="ascii", errors="replace")
    required = (
        "G01 UEFI ENTRY",
        "G01 DISPLAY CONOUT_SUCCESS",
        "UEFI BOOT SERVICES EXITED",
        "MEMORY MAP VALIDATED",
        "G01 UEFI HANDOFF PASS",
        "G01 IDLE",
    )
    for marker in required:
        check(marker in serial, "G01-GUEST", f"guest serial evidence: {marker}")
    check("QEMU guest completed" in qemu_output,
          "G01-QEMU", "actual UEFI guest remains in its idle loop until bounded timeout")
    print("G01 acceptance checks complete.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL G01: {exc}", file=sys.stderr)
        raise SystemExit(1)
