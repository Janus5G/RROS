#!/usr/bin/env python3
"""Validate the native AI Core service layer (host, ASan/UBSan, authority checks)."""

from __future__ import annotations

import os
import re
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


def verify_guest(architecture: str) -> None:
    output = run_command(["python3", "scripts/run-qemu-input.py", architecture, "G13"])
    print(output)
    serial = (ROOT / "build" / architecture / "qemu-serial.log").read_text(
        encoding="ascii", errors="replace")
    for marker in ("G13 AI WORKER ISOLATION PASS", "G13 INFERENCE CANCEL CAPABILITY PASS"):
        check(marker in serial, f"G13-{architecture.upper()}", f"{architecture} QEMU guest reports {marker}")
    check("G13 FAILED" not in serial and "self-test failed" not in serial,
          f"G13-{architecture.upper()}-FAILURE", f"{architecture} guest reported no G13 failure")


def main() -> int:
    output = run_command(["sh", "scripts/test-ai-core-host.sh"])
    print(output)
    check("FAIL G13" not in output, "G13-NO-FAILURES", "host test reported no failures")
    check("PASS G13-AI-CORE:" in output, "G13-HOST", "registry, jobs, cancellation, capabilities and inference pass")
    check("PASS G13-INFERENCE-OUTPUT" in output, "G13-INFERENCE",
          "deterministic local inference output matches the expected continuation")
    check("PASS G13-AI-CORE-SANITIZERS:" in output, "G13-SANITIZERS", "AI core passes ASan and UBSan")

    check("PASS G13-WORKER:" in output and "PASS G13-WORKER-SANITIZERS:" in output, "G13-WORKER-HOST",
          "process-isolated worker passes host, ASan and UBSan runs")
    for architecture in ("x86_64", "aarch64"):
        verify_guest(architecture)

    source = (ROOT / "kernel" / "ai_core.c").read_text(encoding="utf-8")
    worker_source = (ROOT / "kernel" / "ai_worker.c").read_text(encoding="utf-8")
    forbidden = re.compile(r"\b(system|exec[lv]p?e?|popen|fopen|open|write|fork|ioctl|syscall)\s*\(")
    check(not forbidden.search(source) and not forbidden.search(worker_source), "G13-NO-AUTHORITY",
          "AI core has no command, file or hardware execution path for model output")
    check('#include "ai_core.h"' in source and source.count("#include") == 1, "G13-ISOLATED",
          "AI core depends on no kernel or libc facilities beyond its own header")

    progress = (ROOT / "docs" / "G13_PROGRESS.md").read_text(encoding="utf-8")
    check("Not implemented" in progress and "HARDWARE_NOT_VERIFIED" in progress,
          "G13-LIMITS-RECORDED", "progress record lists unimplemented scope and unverified hardware")
    print("G13 software acceptance checks complete; hardware remains HARDWARE_NOT_VERIFIED.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL G13: {exc}", file=sys.stderr)
        raise SystemExit(1)
