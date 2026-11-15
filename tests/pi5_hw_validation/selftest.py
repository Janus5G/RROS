#!/usr/bin/env python3
"""Offline self-test of the hardware-validation tooling (no hardware, no QEMU boot, no writes to devices)."""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
from evaluate_serial_log import evaluate  # noqa: E402


def check(name: str, condition: bool) -> bool:
    print(("PASS " if condition else "FAIL ") + name)
    return condition


def latest_release_log() -> Path | None:
    latest = ROOT / "logs/pi5_uefi_harness/LATEST"
    if not latest.exists():
        return None
    path = ROOT / "logs/pi5_uefi_harness" / latest.read_text().strip() / "release-baseline.serial.log"
    return path if path.exists() else None


def main() -> int:
    ok = True
    qemu_log = latest_release_log()
    if qemu_log:
        result = evaluate(qemu_log.read_text(errors="replace"))
        ok &= check("QEMU release boot log: S1 firmware and S2 handoff PASS",
                    result["stages"]["S1_firmware"]["verdict"] == "PASS"
                    and result["stages"]["S2_handoff"]["verdict"] == "PASS")
        ok &= check("QEMU release boot log is not reported as a clean overall PASS (no G12 fail-closed marker)",
                    result["overall"] == "NOT_PASS")
    else:
        print("SKIP QEMU log cases (no harness log present)")
    ok &= check("empty log -> S1 NOT_OBSERVED", evaluate("")["stages"]["S1_firmware"]["verdict"] == "NOT_OBSERVED")
    fw_only = evaluate("UEFI firmware (version x)\nBdsDxe: starting Boot0001\n")
    ok &= check("firmware-only log -> S1 PASS, S2 NOT_OBSERVED, blocker B1 hint present",
                fw_only["stages"]["S1_firmware"]["verdict"] == "PASS"
                and fw_only["stages"]["S2_handoff"]["verdict"] == "NOT_OBSERVED"
                and any("B1" in n for n in fw_only["notes"]))
    ok &= check("forbidden text blocks PASS", evaluate("UEFI\nG02 FAIL: x\n")["overall"] == "NOT_PASS")
    partial = evaluate("UEFI\nG02 AARCH64 UEFI ENTRY\n")
    ok &= check("partial handoff -> S2 FAIL (not silently NOT_OBSERVED)", partial["stages"]["S2_handoff"]["verdict"] == "FAIL")
    full = "UEFI firmware\n" + "\n".join(["G02 AARCH64 UEFI ENTRY", "G02 EXCEPTION LEVEL VALIDATED", "G02 MMU STATE VALIDATED",
        "G02 EXCEPTION VECTOR PASS", "G03 FAULT DIAGNOSTICS PASS", "G03 MEMORY OWNERSHIP PASS", "G02 MEMORY MAP VALIDATED",
        "G02 UEFI HANDOFF PASS", "G02 IDLE", "G12 HARDWARE DRIVERS NOT VERIFIED"])
    ok &= check("complete synthetic log -> overall PASS", evaluate(full)["overall"] == "PASS")

    with tempfile.TemporaryDirectory() as tmp:
        completed = subprocess.run([sys.executable, str(HERE / "capture_serial.py"), "--out", tmp, "--list"],
                                   capture_output=True, text=True)
        ok &= check("capture_serial.py --list runs (pyserial present)", completed.returncode == 0)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
