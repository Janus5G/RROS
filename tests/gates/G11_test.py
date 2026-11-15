#!/usr/bin/env python3
"""Validate Flow and Classic shared desktop services in host and guests."""

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


def verify_guest(architecture: str, gate: str, markers: tuple[str, ...]) -> None:
    output = run_command([
        "python3", "scripts/run-qemu-input.py", architecture, gate
    ])
    print(output)
    serial_path = ROOT / "build" / architecture / "qemu-serial.log"
    serial = serial_path.read_text(encoding="ascii", errors="replace")
    for marker in markers:
        check(marker in serial,
              f"{gate}-{architecture.upper()}",
              f"{architecture} QEMU guest reports {marker}")
    failure_marker = (
        "G11 Flow desktop self-test failed"
        if gate == "G11"
        else "G10 Classic desktop lifecycle self-test failed"
    )
    check(failure_marker not in serial,
          f"{gate}-{architecture.upper()}-FAILURE",
          f"{architecture} guest reported no {gate} failure")


def main() -> int:
    host_output = run_command(["sh", "scripts/test-flow-host.sh"])
    print(host_output)
    check("PASS G11-FLOW:" in host_output,
          "G11-HOST-FLOW", "dock, switching, theme and service tests pass")
    check("PASS G11-CLASSIC:" in host_output,
          "G11-HOST-CLASSIC", "Classic shared-service regression passes")
    check("PASS G11-FLOW-SANITIZERS:" in host_output,
          "G11-SANITIZERS", "desktop services pass ASan and UBSan")

    for architecture in ("x86_64", "aarch64"):
        verify_guest(
            architecture,
            "G11",
            ("G11 FLOW DESKTOP PASS", "G11 DOCK SWITCH THEME SERVICES PASS"),
        )
        verify_guest(
            architecture,
            "G10",
            (
                "G10 CLASSIC DESKTOP PASS",
                "G10 LAUNCH INPUT RESIZE CLOSE CLEANUP PASS",
            ),
        )

    print("G11 acceptance checks complete.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL G11: {exc}", file=sys.stderr)
        raise SystemExit(1)
