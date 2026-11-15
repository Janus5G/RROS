#!/usr/bin/env python3
"""Validate timer events, firmware input and handoff in x86-64 and AArch64 guests."""

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


def run_input_test(architecture: str) -> str:
    command = ["python3", "scripts/run-qemu-input.py", architecture]
    print(f"COMMAND: {' '.join(command)} (inside WSL)")
    if os.name == "nt":
        root = subprocess.run(
            ["wsl", "--exec", "wslpath", "-a", str(ROOT)],
            text=True, encoding="utf-8", capture_output=True, check=True,
        ).stdout.strip()
        shell_command = "cd " + shlex.quote(root) + " && " + " ".join(
            shlex.quote(part) for part in command
        )
        result = subprocess.run(
            ["wsl", "--exec", "sh", "-lc", shell_command],
            text=True, encoding="utf-8", stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            check=False,
        )
    else:
        result = subprocess.run(
            command, cwd=ROOT, text=True, encoding="utf-8", stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, check=False,
        )
    if result.returncode:
        raise RuntimeError(f"G04 {architecture} test failed:\n{result.stdout}")
    return result.stdout.strip()


def main() -> int:
    firmware_header = ROOT / "boot" / "uefi" / "efi.h"
    timer_core = ROOT / "kernel" / "g04.c"
    check(firmware_header.is_file() and timer_core.is_file(),
          "G04-SOURCE", "UEFI timer, console-input and device interfaces exist")

    for architecture in ("x86_64", "aarch64"):
        output = run_input_test(architecture)
        print(output)
        check("PASS G04-TIMER" in output,
              "G04-TIMER", f"{architecture} guest timer test completed")
        check("PASS G04-INPUT" in output,
              "G04-INPUT", f"{architecture} guest received the injected key")

    print("G04 acceptance checks complete.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL G04: {exc}", file=sys.stderr)
        raise SystemExit(1)
