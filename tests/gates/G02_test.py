#!/usr/bin/env python3
"""Build and boot the AArch64 UEFI foundation in the QEMU virt machine."""

from __future__ import annotations

import os
import shlex
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
IMAGE = ROOT / "build" / "dev" / "aarch64" / "esp" / "EFI" / "BOOT" / "BOOTAA64.EFI"
SERIAL_LOG = ROOT / "build" / "aarch64" / "qemu-serial.log"


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
    source = ROOT / "boot" / "aarch64" / "efi_main.c"
    vectors = ROOT / "boot" / "aarch64" / "vectors.S"
    check(source.is_file() and vectors.is_file(),
          "G02-SOURCE", "AArch64 EFI and exception-vector sources exist")

    build_output = run_script("scripts/build-aarch64.sh")
    print(build_output)
    check(IMAGE.is_file() and IMAGE.stat().st_size > 512,
          "G02-IMAGE", "native AArch64 EFI image was built")
    data = IMAGE.read_bytes()
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    check(data[:2] == b"MZ" and data[pe_offset:pe_offset + 4] == b"PE\0\0",
          "G02-PE", "AArch64 image has valid PE/COFF headers")
    check(struct.unpack_from("<H", data, pe_offset + 4)[0] == 0xAA64,
          "G02-MACHINE", "EFI image targets AArch64")
    check(struct.unpack_from("<H", data, pe_offset + 24 + 68)[0] == 10,
          "G02-EFI-SUBSYSTEM", "image is an EFI application")

    qemu_output = run_script("scripts/run-qemu-aarch64.sh")
    print(qemu_output)
    serial = SERIAL_LOG.read_text(encoding="ascii", errors="replace")
    required = (
        "G02 AARCH64 UEFI ENTRY",
        "G02 EXCEPTION LEVEL VALIDATED",
        "G02 MMU STATE VALIDATED",
        "G02 EXCEPTION VECTOR PASS",
        "G02 MEMORY MAP VALIDATED",
        "G02 UEFI HANDOFF PASS",
        "G02 IDLE",
    )
    for marker in required:
        check(marker in serial, "G02-GUEST", f"guest serial evidence: {marker}")
    check("AArch64 UEFI guest validated" in qemu_output,
          "G02-QEMU", "actual ARM guest reached UEFI handoff and safe idle")
    check("QEMU emulator version 10.2.1" in qemu_output,
          "G02-EMU-VERSION", "recorded QEMU ARM version matches the validated toolchain")
    print("G02 acceptance checks complete.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL G02: {exc}", file=sys.stderr)
        raise SystemExit(1)
