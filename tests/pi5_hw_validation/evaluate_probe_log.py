#!/usr/bin/env python3
"""Evaluate a serial log produced by the isolated RROS Pi 5 hardware probe.

Modes:
  qemu  generic AArch64 QEMU/AAVMF regression. Never hardware evidence.
  pi5   serial capture from a physical Raspberry Pi 5. Accepts only a log that
        contains the full marker set and a Pi 5 device tree.

Classifications printed (exit code in parentheses):
  SOFTWARE_PASS (0)                  qemu mode, every required check passed
  PI5_PROBE_PASS (0)                 pi5 mode, every required check passed on a log supplied by the owner
  FAIL (1)                           a required check failed or a marker is missing
  BLOCKED_BY_PHYSICAL_HARDWARE (2)   pi5 mode with no log (nothing to evaluate)
Probe output is duplicated when UEFI ConOut is also the serial port; checks are de-duplicated.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

COMMON = [
    "handoff-uefi-system-table", "config-table-fdt-present", "cpu-exception-level-1-or-2",
    "cpu-firmware-mmu-enabled", "cpu-physical-address-range", "exception-brk-vector-entered",
    "exception-vbar-restored", "memmap-validates-for-mmu-handoff", "memmap-conventional-memory-present",
    "dt-blob-found", "dt-parses-with-production-parser", "uart-stdout-resolved",
    "uart-fdt-console-active", "gic-controller-described-in-dt", "timer-frequency-reported",
    "timer-counter-advances-with-firmware-stall",
]
PI5_ONLY = ["platform-pcie-controller-enabled-in-dt", "platform-rp1-described-in-dt"]
PI5_UART_BASE = 0x107D001000
CHECK = re.compile(r"PI5PROBE CHECK name=(\S+) result=(PASS|FAIL)(?=\s|$)")
END = re.compile(
    r"^PI5PROBE END pass=(\d+) fail=(\d+) result=(DIAGNOSTICS-COMPLETE|DIAGNOSTICS-FAILURES)(?=\s|$)",
    re.MULTILINE,
)


def evaluate(text: str, mode: str) -> tuple[str, list[str]]:
    problems: list[str] = []
    results: dict[str, str] = {}
    for name, result in CHECK.findall(text):
        if results.get(name) == "FAIL":
            continue
        results[name] = result
    required = COMMON + (PI5_ONLY if mode == "pi5" else [])
    for name in required:
        if name not in results:
            problems.append(f"missing check {name}")
        elif results[name] != "PASS":
            problems.append(f"check failed {name}")
    for name, result in results.items():
        if result == "FAIL" and name not in required and not (mode == "qemu" and name in PI5_ONLY):
            problems.append(f"check failed {name}")
    if "PI5PROBE BEGIN" not in text:
        problems.append("missing PI5PROBE BEGIN")
    summaries = set(END.findall(text))
    end_lines = re.findall(r"^PI5PROBE END[^\r\n]*", text, re.MULTILINE)
    if not summaries or len(END.findall(text)) != len(end_lines):
        problems.append("missing PI5PROBE END (probe did not finish or output was lost)")
    elif len(summaries) != 1:
        problems.append("conflicting PI5PROBE END summaries")
    else:
        passed, failed, result = next(iter(summaries))
        actual_failed = sum(value == "FAIL" for value in results.values())
        actual_passed = sum(value == "PASS" for value in results.values())
        if int(passed) != actual_passed or int(failed) != actual_failed:
            problems.append("END counters do not match the de-duplicated checks")
        if (int(failed) == 0) != (result == "DIAGNOSTICS-COMPLETE"):
            problems.append("END result contradicts its failure counter")
    if mode == "pi5":
        identities = re.findall(r"^PI5PROBE DT [^\r\n]*?\broot_compatible=(\S+)", text, re.MULTILINE)
        if not identities or any(identity not in {"raspberrypi,5", "raspberrypi,5-model-b"} for identity in identities):
            problems.append("device-tree root compatible is not Raspberry Pi 5")
        addresses = re.findall(
            r"^PI5PROBE UART resolved=1 [^\r\n]*?\bbase=0[xX]([0-9a-fA-F]{1,16})(?=\s|$)",
            text, re.MULTILINE,
        )
        uart_lines = re.findall(r"^PI5PROBE UART [^\r\n]*", text, re.MULTILINE)
        if not addresses or len(addresses) != len(uart_lines) or any(int(address, 16) != PI5_UART_BASE for address in addresses):
            problems.append(f"UART base is not the expected debug UART {PI5_UART_BASE:#x}")
    elif "PI5PROBE UART resolved=1" not in text:
        problems.append("QEMU UART was not resolved from the device tree")
    if problems:
        return "FAIL", problems
    return ("PI5_PROBE_PASS" if mode == "pi5" else "SOFTWARE_PASS"), []


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--mode", choices=("qemu", "pi5"), required=True)
    parser.add_argument("log", type=Path)
    args = parser.parse_args()
    if not args.log.is_file() or args.log.stat().st_size == 0:
        if args.mode == "pi5":
            print("BLOCKED_BY_PHYSICAL_HARDWARE: no serial capture supplied")
            return 2
        print("FAIL: empty or missing log")
        return 1
    text = args.log.read_text(encoding="utf-8", errors="replace")
    verdict, problems = evaluate(text, args.mode)
    print(f"{verdict}: mode={args.mode} log={args.log}")
    for problem in problems:
        print(f"  - {problem}")
    if args.mode == "qemu" and verdict == "SOFTWARE_PASS":
        print("  note: virtual platform only; NOT Raspberry Pi 5 hardware evidence (HARDWARE_NOT_VERIFIED)")
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
