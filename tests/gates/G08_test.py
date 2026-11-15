#!/usr/bin/env python3
"""Validate the framebuffer compositor and interactive window input in guests."""

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
        raise RuntimeError(f"Command failed ({result.returncode}):\n{result.stdout}")
    return result.stdout.strip()


def main() -> int:
    host_output = run_command(["sh", "scripts/test-graphics-host.sh"])
    print(host_output)
    check("PASS G08-GRAPHICS:" in host_output,
          "G08-HOST", "RGB/BGR, window compositor and pointer-event tests pass")
    check("PASS G08-GRAPHICS-SANITIZERS:" in host_output,
          "G08-SANITIZERS", "graphics tests pass AddressSanitizer and UBSan")

    for architecture in ("x86_64", "aarch64"):
        output = run_command([
            "python3", "scripts/run-qemu-input.py", architecture, "G08"
        ])
        print(output)
        serial_path = ROOT / "build" / architecture / "qemu-serial.log"
        serial = serial_path.read_text(encoding="ascii", errors="replace")
        for marker in (
            "G08 FRAMEBUFFER WINDOW COMPOSITOR PASS",
            "G08 INPUT EVENTS PASS",
        ):
            check(marker in serial,
                  f"G08-{architecture.upper()}",
                  f"{architecture} QEMU guest reports {marker}")
        check("G08 framebuffer/window/compositor self-test failed" not in serial,
              f"G08-{architecture.upper()}-FAILURE",
              f"{architecture} guest reported no graphics failure")

    print("G08 acceptance checks complete.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL G08: {exc}", file=sys.stderr)
        raise SystemExit(1)
