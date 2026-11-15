#!/usr/bin/env python3
"""Validate the graphics pipeline (software renderer; Pi 5 GPU transport is fail-closed)."""

from __future__ import annotations

import os
import re
import shlex
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
KEY_IDS = [
    "G19-DAMAGE-LIST-BOUNDED-BY-BOUNDING-BOX-FALLBACK",
    "G19-GPU-API-SUPPORT-TABLE-HONEST-ABOUT-V3D-VULKAN-AND-CONFORMANCE",
    "G19-GPU-V3D-BACKEND-FAILS-CLOSED-AND-WRITES-NOTHING",
    "G19-GPU-FALLBACK-TO-SOFTWARE-WHEN-HARDWARE-UNAVAILABLE",
    "G19-GPU-HARDWARE-IS-NEVER-USED-AS-FALLBACK",
]


def check(condition: bool, test_id: str, message: str) -> None:
    if not condition:
        raise AssertionError(f"FAIL {test_id}: {message}")
    print(f"PASS {test_id}: {message}")


def run_command(command: list[str]) -> str:
    print(f"COMMAND: {' '.join(command)}")
    if os.name == "nt":
        root = subprocess.run(["wsl", "--exec", "wslpath", "-a", str(ROOT)], text=True,
                              encoding="utf-8", capture_output=True, check=True).stdout.strip()
        argv = ["wsl", "--exec", "sh", "-lc", "cd " + shlex.quote(root) + " && " +
                " ".join(shlex.quote(p) for p in command)]
    else:
        argv = command
    result = subprocess.run(argv, cwd=ROOT, text=True, encoding="utf-8", stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, check=False)
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}):\n{result.stdout}")
    return result.stdout.strip()


def verify_guest(architecture: str) -> None:
    output = run_command(["python3", "scripts/run-qemu-input.py", architecture, "G19"])
    print(output)
    serial = (ROOT / "build" / architecture / "qemu-serial.log").read_text(
        encoding="ascii", errors="replace")
    for marker in ("G19 GFX DAMAGE BUFFER SYNC PASS", "G19 GPU FALLBACK FAIL-CLOSED PASS"):
        check(marker in serial, f"G19-{architecture.upper()}", f"{architecture} QEMU guest reports {marker}")
    check("G19 FAILED" not in serial and "self-test failed" not in serial,
          f"G19-{architecture.upper()}-FAILURE", f"{architecture} guest reported no G19 failure")
    match = re.search(r"G19 MEASURE pixels damage=(\d+) full=(\d+) ticks damage=(\d+) full=(\d+)", serial)
    check(match is not None, f"G19-{architecture.upper()}-MEASURED", "guest reported measured pixel and tick counts")
    damage, full = int(match.group(1)), int(match.group(2))
    check(damage * 10 < full, f"G19-{architecture.upper()}-PIXEL-REDUCTION",
          f"guest damage tracking wrote {damage} pixels vs {full} for full repaint (>10x fewer)")


def main() -> int:
    output = run_command(["sh", "scripts/test-gfx-host.sh"])
    print(output)
    check("FAIL G19" not in output, "G19-NO-FAILURES", "host tests reported no failures")
    check(output.count("G19 GFX RESULT: PASS") == 2, "G19-GFX-HOST", "graphics tests pass in plain and ASan/UBSan builds")
    source = (ROOT / "tests" / "gates" / "G19_gfx_test.c").read_text(encoding="utf-8")
    ids = sorted(set(re.findall(r'"(G19-[A-Z0-9-]+)"\)', source)))
    check(len(ids) >= 50, "G19-CHECK-COUNT", f"{len(ids)} distinct acceptance checks defined")
    for test_id in ids:
        check(output.count(f"PASS {test_id}\n") + (1 if output.endswith(f"PASS {test_id}") else 0) == 2
              or output.count(f"PASS {test_id}") == 2, test_id, "check present and passing in both builds")
    for test_id in KEY_IDS:
        check(test_id in ids, f"KEY-{test_id}", "key safety check is defined")
    for architecture in ("x86_64", "aarch64"):
        verify_guest(architecture)

    forbidden = re.compile(r"\b(system|exec[lv]p?e?|popen|fopen|open|fork|ioctl|syscall|socket|connect|bind|listen)\s*\(")
    for name in ("gfx.c", "gfx_selftest.c"):
        text = (ROOT / "kernel" / name).read_text(encoding="utf-8")
        check(not forbidden.search(text), f"G19-NO-AUTHORITY-{name}", f"{name} has no process, file, socket or hardware path")

    progress = (ROOT / "docs" / "G19_PROGRESS.md").read_text(encoding="utf-8")
    for needle in ("Not implemented", "HARDWARE_NOT_VERIFIED", "compatibility-pending"):
        check(needle in progress, f"G19-DOC-{needle.upper().replace(' ', '-')}", f"progress record states '{needle}'")
    print("G19 software acceptance checks complete; hardware remains HARDWARE_NOT_VERIFIED.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL G19: {exc}", file=sys.stderr)
        raise SystemExit(1)
