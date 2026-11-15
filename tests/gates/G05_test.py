#!/usr/bin/env python3
"""Validate G05 process isolation and lifecycle in both architecture guests."""

from __future__ import annotations

import os
import shlex
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GUEST_MARKERS = (
    "G05 USER ISOLATION PASS",
    "G05 SYSCALL SCHEDULER CLEANUP PASS",
)


def check(condition: bool, test_id: str, message: str) -> None:
    if not condition:
        raise AssertionError(f"FAIL {test_id}: {message}")
    print(f"PASS {test_id}: {message}")


def run_script(script: str, guest_test: bool = False) -> str:
    print(f"COMMAND: {' '.join((['EXPERIMENTALOS_G05_TEST=1'] if guest_test else []) + ['sh', script])} (inside WSL)")
    if os.name == "nt":
        root = subprocess.run(
            ["wsl", "--exec", "wslpath", "-a", str(ROOT)],
            text=True, encoding="utf-8", capture_output=True, check=True,
        ).stdout.strip()
        prefix = "EXPERIMENTALOS_G05_TEST=1 " if guest_test else ""
        command = (
            f"cd {shlex.quote(root)} && {prefix}sh {shlex.quote(script)}"
        )
        result = subprocess.run(
            ["wsl", "--exec", "sh", "-lc", command],
            text=True, encoding="utf-8", stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            check=False,
        )
    else:
        environment = os.environ.copy()
        if guest_test:
            environment["EXPERIMENTALOS_G05_TEST"] = "1"
        result = subprocess.run(
            ["sh", str(ROOT / script)], cwd=ROOT, env=environment,
            text=True, encoding="utf-8", stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            check=False,
        )
    if result.returncode:
        raise RuntimeError(f"{script} failed ({result.returncode}):\n{result.stdout}")
    return result.stdout.strip()


def main() -> int:
    host_output = run_script("scripts/test-process-host.sh")
    print(host_output)
    check("PASS G05-PROCESS-HOST" in host_output,
          "G05-HOST", "scheduler, syscall validation, lifecycle and concurrency pass")
    check("PASS G05-PROCESS-SANITIZERS" in host_output,
          "G05-SANITIZERS", "process/thread tests pass AddressSanitizer and UBSan")

    for architecture, script, log_path in (
        (
            "x86-64",
            "scripts/run-qemu-x86_64.sh",
            ROOT / "build" / "x86_64" / "qemu-serial.log",
        ),
        (
            "AArch64",
            "scripts/run-qemu-aarch64.sh",
            ROOT / "build" / "aarch64" / "qemu-serial.log",
        ),
    ):
        output = run_script(script, guest_test=True)
        print(output)
        serial = log_path.read_text(encoding="ascii", errors="replace")
        for marker in GUEST_MARKERS:
            check(marker in serial, f"G05-{architecture.upper()}",
                  f"{architecture} guest emitted {marker}")
        check("G05 KERNEL CHECK FAIL" not in serial,
              f"G05-{architecture.upper()}-FAILURE",
              f"{architecture} guest reported no kernel-check failure")

    print("G05 acceptance checks complete.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL G05: {exc}", file=sys.stderr)
        raise SystemExit(1)
