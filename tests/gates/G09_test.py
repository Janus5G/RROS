#!/usr/bin/env python3
"""Validate native image loading, SDK compilation and isolated guest execution."""

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


def run_script(script: str, guest_test: bool = False) -> str:
    prefix = "EXPERIMENTALOS_G09_TEST=1 " if guest_test else ""
    print(f"COMMAND: {prefix}sh {script} (inside WSL)")
    if os.name == "nt":
        root = subprocess.run(
            ["wsl", "--exec", "wslpath", "-a", str(ROOT)],
            text=True, encoding="utf-8", capture_output=True, check=True,
        ).stdout.strip()
        command = f"cd {shlex.quote(root)} && {prefix}sh {shlex.quote(script)}"
        result = subprocess.run(
            ["wsl", "--exec", "sh", "-lc", command],
            text=True, encoding="utf-8", stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            check=False,
        )
    else:
        environment = os.environ.copy()
        if guest_test:
            environment["EXPERIMENTALOS_G09_TEST"] = "1"
        result = subprocess.run(
            ["sh", str(ROOT / script)], cwd=ROOT, env=environment,
            text=True, encoding="utf-8", stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            check=False,
        )
    if result.returncode:
        raise RuntimeError(f"{script} failed ({result.returncode}):\n{result.stdout}")
    return result.stdout.strip()


def main() -> int:
    host_output = run_script("scripts/test-app-loader-host.sh")
    print(host_output)
    check("PASS G09-APP-LOADER:" in host_output,
          "G09-LOADER", "valid and malformed versioned images are handled safely")
    check("PASS G09-LOADER-SANITIZERS:" in host_output,
          "G09-SANITIZERS", "loader tests pass AddressSanitizer and UBSan")
    check("PASS G09-SDK-CROSS-COMPILE:" in host_output,
          "G09-SDK", "native SDK example compiles for both architectures")

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
        for marker in (
            "G09 VERSIONED APP EXECUTION PASS",
            "G09 NATIVE ABI V1 PASS",
        ):
            check(marker in serial,
                  f"G09-{architecture.upper()}",
                  f"{architecture} isolated guest emitted {marker}")
        check("G05 KERNEL CHECK FAIL" not in serial,
              f"G09-{architecture.upper()}-ISOLATION",
              f"{architecture} guest process isolation/lifecycle remained intact")

    print("G09 acceptance checks complete.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL G09: {exc}", file=sys.stderr)
        raise SystemExit(1)
