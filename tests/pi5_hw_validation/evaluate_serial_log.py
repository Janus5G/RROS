#!/usr/bin/env python3
"""Classify a captured serial log against the first-boot acceptance stages for a Raspberry Pi 5.

Stages are evaluated independently so that a missing RROS marker is never mistaken for a firmware failure:
  S1 firmware     - the Pi 5 UEFI/TF-A firmware printed to the debug UART (boot chain alive)
  S2 handoff      - RROS boot-application markers (G02/G03 handoff) were seen
  S3 fail-closed  - the driver layer reported unverified hardware and made no operational claim
Verdicts: PASS / FAIL / NOT_OBSERVED. Exit code 0 only if every stage PASSes and nothing forbidden is present.
This tool classifies evidence; it never upgrades a result.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

FIRMWARE_PATTERNS = [r"UEFI firmware", r"BdsDxe", r"BL31", r"TF-?A", r"EDK2|edk2", r"Raspberry Pi", r"\bUEFI\b"]
HANDOFF_MARKERS = [
    "G02 AARCH64 UEFI ENTRY", "G02 EXCEPTION LEVEL VALIDATED", "G02 MMU STATE VALIDATED",
    "G02 EXCEPTION VECTOR PASS", "G03 FAULT DIAGNOSTICS PASS", "G03 MEMORY OWNERSHIP PASS",
    "G02 MEMORY MAP VALIDATED", "G02 UEFI HANDOFF PASS", "G02 IDLE",
]
FAIL_CLOSED_MARKERS = ["G12 HARDWARE DRIVERS NOT VERIFIED"]
FORBIDDEN = ["HARDWARE VERIFIED", "DRIVERS OPERATIONAL", "PANIC", "UNHANDLED EXCEPTION", "G02 FAIL"]
SOFTWARE_BLOCKER_HINT = (
    "Firmware output is present but no RROS marker was seen. Known software blocker B1: the release boot "
    "application writes its markers to the QEMU virt PL011 address 0x09000000, which is not the Pi 5 debug UART. "
    "Silence from RROS is therefore expected and is NOT evidence of a boot-chain failure or of a pass."
)


def evaluate(text: str) -> dict:
    upper = text.upper()
    firmware_hits = [p for p in FIRMWARE_PATTERNS if re.search(p, text)]
    handoff_missing = [m for m in HANDOFF_MARKERS if m not in text]
    closed_missing = [m for m in FAIL_CLOSED_MARKERS if m not in text]
    forbidden = [m for m in FORBIDDEN if m.upper() in upper]

    stages = {
        "S1_firmware": ("PASS" if firmware_hits else "NOT_OBSERVED",
                        f"firmware patterns seen: {firmware_hits}" if firmware_hits else "no firmware text on the UART"),
        "S2_handoff": ("PASS" if not handoff_missing else
                       ("FAIL" if any(m in text for m in HANDOFF_MARKERS) else "NOT_OBSERVED"),
                       "all handoff markers seen" if not handoff_missing else "missing: " + "; ".join(handoff_missing)),
        "S3_fail_closed": ("PASS" if not closed_missing else "NOT_OBSERVED",
                           "driver layer reported hardware not verified" if not closed_missing
                           else "marker absent (only emitted by the G12 test build): " + "; ".join(closed_missing)),
    }
    notes = []
    if forbidden:
        notes.append("forbidden text present: " + ", ".join(forbidden))
    if stages["S1_firmware"][0] == "PASS" and stages["S2_handoff"][0] == "NOT_OBSERVED":
        notes.append(SOFTWARE_BLOCKER_HINT)
    if stages["S1_firmware"][0] != "PASS":
        notes.append("No firmware text: check wiring (adapter RX<->Pi TX), 3.3 V adapter, 115200 8N1, firmware card contents.")
    ok = all(v[0] == "PASS" for v in stages.values()) and not forbidden
    return {"stages": {k: {"verdict": v[0], "detail": v[1]} for k, v in stages.items()},
            "forbidden": forbidden, "notes": notes, "overall": "PASS" if ok else "NOT_PASS"}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    result = evaluate(args.log.read_text(errors="replace"))
    if args.json:
        print(json.dumps(result, indent=2))
    else:
        for name, stage in result["stages"].items():
            print(f"{stage['verdict']:13} {name}: {stage['detail']}")
        for note in result["notes"]:
            print(f"NOTE: {note}")
        print(f"OVERALL: {result['overall']}")
    return 0 if result["overall"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
