#!/usr/bin/env python3
"""Validate the isolated G07 network stack in host and architecture guests."""

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


def run_script(
    script: str,
    guest_test: bool = False,
) -> str:
    prefix = "EXPERIMENTALOS_G07_TEST=1 " if guest_test else ""
    print(f"COMMAND: {prefix}sh {script} (inside WSL)")
    if os.name == "nt":
        root = subprocess.run(
            ["wsl", "--exec", "wslpath", "-a", str(ROOT)],
            text=True, encoding="utf-8", capture_output=True, check=True,
        ).stdout.strip()
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
            environment["EXPERIMENTALOS_G07_TEST"] = "1"
        result = subprocess.run(
            ["sh", str(ROOT / script)], cwd=ROOT, env=environment,
            text=True, encoding="utf-8", stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            check=False,
        )
    if result.returncode:
        raise RuntimeError(f"{script} failed ({result.returncode}):\n{result.stdout}")
    return result.stdout.strip()


def main() -> int:
    host_output = run_script("scripts/test-network-host.sh")
    print(host_output)
    check("PASS G07-NETWORK:" in host_output,
          "G07-HOST", "bounded VM NIC protocol tests pass")
    check("PASS G07-NETWORK-SANITIZERS:" in host_output,
          "G07-SANITIZERS", "network tests pass AddressSanitizer and UBSan")

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
        check("G07 VM NETWORK DRIVER PASS" in serial,
              f"G07-{architecture.upper()}",
              f"{architecture} QEMU guest passed isolated VM NIC tests")
        check("G07 DHCP DNS TCP UDP PASS" in serial,
              f"G07-{architecture.upper()}-PROTOCOLS",
              f"{architecture} guest passed DHCP/DNS/TCP/UDP and ARP/ICMP")
        check("G07 network self-test failed" not in serial,
              f"G07-{architecture.upper()}-FAILURE",
              f"{architecture} guest reported no network failure")

    print("G07 acceptance checks complete.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL G07: {exc}", file=sys.stderr)
        raise SystemExit(1)
