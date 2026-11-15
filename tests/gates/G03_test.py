#!/usr/bin/env python3
"""Validate shared memory services in host tests and both UEFI guests."""

from __future__ import annotations

import os
import shlex
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


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
    memory_source = ROOT / "kernel" / "memory.c"
    memory_test = ROOT / "tests" / "gates" / "G03_memory_test.c"
    check(memory_source.is_file() and memory_test.is_file(),
          "G03-SOURCE", "shared memory implementation and adversarial tests exist")

    host_output = run_script("scripts/test-memory-host.sh")
    print(host_output)
    check("PASS G03-MEMORY-HOST" in host_output,
          "G03-HOST", "normal and malformed memory operations pass host tests")
    check("PASS G03-MEMORY-SANITIZERS" in host_output,
          "G03-SANITIZERS", "memory tests pass AddressSanitizer and UBSan")

    for architecture, script, log_path, handoff in (
        (
            "x86-64",
            "scripts/run-qemu-x86_64.sh",
            ROOT / "build" / "x86_64" / "qemu-serial.log",
            "G01 UEFI HANDOFF PASS",
        ),
        (
            "AArch64",
            "scripts/run-qemu-aarch64.sh",
            ROOT / "build" / "aarch64" / "qemu-serial.log",
            "G02 UEFI HANDOFF PASS",
        ),
    ):
        output = run_script(script)
        print(output)
        serial = log_path.read_text(encoding="ascii", errors="replace")
        check("G03 FAULT DIAGNOSTICS PASS" in serial,
              "G03-FAULTS", f"{architecture} guest captured and recovered a debug trap")
        check("G03 MEMORY OWNERSHIP PASS" in serial,
              "G03-GUEST", f"{architecture} guest validated a firmware-backed page arena")
        check(handoff in serial,
              "G03-REGRESSION", f"{architecture} guest retained its firmware handoff")

    print("G03 acceptance checks complete.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL G03: {exc}", file=sys.stderr)
        raise SystemExit(1)
