#!/usr/bin/env python3
"""Validate the Classic desktop host model and both UEFI guests."""

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


def run_command(command: list[str]) -> str:
    print(f"COMMAND: {' '.join(command)}")
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
        raise RuntimeError(
            f"Command failed ({result.returncode}):\n{result.stdout}")
    return result.stdout.strip()


def main() -> int:
    host_output = run_command(["sh", "scripts/test-desktop-host.sh"])
    print(host_output)
    check("PASS G10-DESKTOP:" in host_output,
          "G10-HOST", "desktop rendering, lifecycle and invalid input tests pass")
    check("PASS G10-DESKTOP-SANITIZERS:" in host_output,
          "G10-SANITIZERS", "desktop and process model pass ASan and UBSan")

    for architecture in ("x86_64", "aarch64"):
        output = run_command([
            "python3", "scripts/run-qemu-input.py", architecture, "G10"
        ])
        print(output)
        serial_path = ROOT / "build" / architecture / "qemu-serial.log"
        serial = serial_path.read_text(encoding="ascii", errors="replace")
        for marker in (
            "G10 CLASSIC DESKTOP PASS",
            "G10 LAUNCH INPUT RESIZE CLOSE CLEANUP PASS",
        ):
            check(marker in serial,
                  f"G10-{architecture.upper()}",
                  f"{architecture} QEMU guest reports {marker}")
        check("G10 Classic desktop lifecycle self-test failed" not in serial,
              f"G10-{architecture.upper()}-FAILURE",
              f"{architecture} guest reported no desktop failure")

    print("G10 acceptance checks complete.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL G10: {exc}", file=sys.stderr)
        raise SystemExit(1)
