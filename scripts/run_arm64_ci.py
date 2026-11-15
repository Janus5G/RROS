#!/usr/bin/env python3
"""Run native ARM64 host regressions and write structured evidence."""

from __future__ import annotations

import hashlib
import json
import os
import platform
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build" / "arm64-ci"
RESULT = BUILD / "result.json"
HASHES = BUILD / "SHA256SUMS.txt"
TESTS = (
    ("G03 host memory", "scripts/test-memory-host.sh"),
    ("G05 host process and sanitizers", "scripts/test-process-host.sh"),
    ("G06 host storage and sanitizers", "scripts/test-storage-host.sh"),
    ("G07 host network and sanitizers", "scripts/test-network-host.sh"),
)
EVIDENCE_FILES = (
    "scripts/run_arm64_ci.py",
    "boot/aarch64/efi_main.c",
    "boot/aarch64/vectors.S",
    "boot/uefi/efi.h",
    "scripts/test-memory-host.sh",
    "scripts/test-process-host.sh",
    "scripts/test-storage-host.sh",
    "scripts/test-network-host.sh",
    "scripts/test-pi5-host.sh",
    "kernel/memory.c",
    "kernel/aarch64_mmu.c",
    "kernel/aarch64_mmu.h",
    "kernel/aarch64_mmu_runtime.c",
    "kernel/aarch64_mmu_runtime.h",
    "kernel/aarch64_mmu_uefi.c",
    "kernel/aarch64_mmu_uefi.h",
    "kernel/process.c",
    "kernel/storage.c",
    "kernel/network.c",
    "kernel/device_tree.c",
    "kernel/device_tree_test.c",
    "kernel/pi5_platform.c",
    "kernel/pi5_driver.c",
    "kernel/pi5_driver_test.c",
    "tests/gates/G12_aarch64_mmu_test.c",
    "tests/gates/G12_aarch64_mmu_runtime_test.c",
    "tests/gates/G12_test.py",
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def source_revision() -> str:
    result = subprocess.run(
        ["git", "-C", str(ROOT), "rev-parse", "HEAD"],
        text=True, encoding="utf-8",
        capture_output=True,
        check=False,
    )
    return result.stdout.strip() if result.returncode == 0 else "UNAVAILABLE"


def write_evidence(report: dict[str, object]) -> None:
    BUILD.mkdir(parents=True, exist_ok=True)
    report["sha256"] = {
        relative: sha256(ROOT / relative)
        for relative in EVIDENCE_FILES
        if (ROOT / relative).is_file()
    }
    RESULT.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    manifest = "".join(
            f"{digest}  {relative}\n"
            for relative, digest in sorted(report["sha256"].items())
    )
    HASHES.write_text(
        manifest + f"{sha256(RESULT)}  {RESULT.relative_to(ROOT)}\n",
        encoding="ascii",
    )


def main() -> int:
    architecture = platform.machine().lower()
    report: dict[str, object] = {
        "schema_version": 1,
        "started_at": datetime.now(timezone.utc).isoformat(),
        "source_commit": source_revision(),
        "platform": platform.platform(),
        "architecture": architecture,
        "execution_environment": (
            "github_actions"
            if os.environ.get("GITHUB_ACTIONS") == "true"
            else "local"
        ),
        "github_execution_status": (
            "EXECUTED"
            if os.environ.get("GITHUB_ACTIONS") == "true"
            else "NOT_EXECUTED"
        ),
        "github_actions_run": os.environ.get("GITHUB_RUN_ID"),
        "g12_status": "IN_PROGRESS",
        "g13_status": "NOT_STARTED",
        "hardware_status": "HARDWARE_NOT_VERIFIED",
        "results": [],
    }

    if architecture not in {"aarch64", "arm64"}:
        report["status"] = "FAIL"
        report["error"] = (
            f"Native ARM64 runner required; observed architecture={architecture}"
        )
        report["finished_at"] = datetime.now(timezone.utc).isoformat()
        write_evidence(report)
        print(report["error"], file=sys.stderr)
        return 2

    results: list[dict[str, object]] = []
    report["results"] = results
    for label, script in TESTS:
        command = ["sh", str(ROOT / script)]
        completed = subprocess.run(
            command,
            cwd=ROOT,
            text=True, encoding="utf-8",
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        result = {
            "name": label,
            "command": command,
            "exit_code": completed.returncode,
            "status": "PASS" if completed.returncode == 0 else "FAIL",
            "output": completed.stdout,
        }
        results.append(result)
        print(f"## {label}: {result['status']}")
        print(completed.stdout, end="" if completed.stdout.endswith("\n") else "\n")

    failed = [result for result in results if result["exit_code"] != 0]
    results.append({
        "name": "G12 native ARM64 host driver/cache suite",
        "command": None, "exit_code": None, "status": "NOT_TESTABLE",
        "reason": (
            "The existing combined suite reaches AArch64 dc ivac cache maintenance, "
            "which traps in hosted EL0. Do not execute privileged cache tests on "
            "a Linux-hosted ARM64 runner. G12 remains covered by x86 host register "
            "models and privileged AArch64 QEMU guest fixtures in release CI."
        ),
    })
    report["status"] = "FAIL" if failed else "PASS"
    report["interpretation"] = (
        "Four native ARM64 host-software suites only. The combined G12 native "
        "host suite is NOT_TESTABLE (privileged cache instructions); "
        "this report does not assert complete G12 acceptance or Raspberry Pi 5 "
        "hardware verification."
    )
    report["finished_at"] = datetime.now(timezone.utc).isoformat()
    write_evidence(report)
    print(f"RESULT_JSON: {RESULT.relative_to(ROOT)}")
    print(f"SHA256_MANIFEST: {HASHES.relative_to(ROOT)}")
    print(f"CI_TEST_STATUS: {report['status']}")
    print("G12_GATE_STATUS: IN_PROGRESS")
    print("G12_NATIVE_HOST_STATUS: NOT_TESTABLE")
    print("HARDWARE_STATUS: HARDWARE_NOT_VERIFIED")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
