#!/usr/bin/env python3
"""Validate developer tools, EOSVM runtime, editor and the Development & Deployment package manager (host-tested)."""

from __future__ import annotations

import os
import re
import shlex
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

REQUIRED = [
    "G17-VM-ARITHMETIC-PROGRAM-EXECUTES", "G17-VM-DIVIDE-BY-ZERO-TRAPS", "G17-VM-FUEL-BOUNDS-INFINITE-LOOP",
    "G17-VM-OUTPUT-BOUNDED", "G17-VM-MALFORMED-BYTECODE-REJECTED-BEFORE-RUN", "G17-ASM-REJECTS-MALFORMED-SOURCE",
    "G17-SDK-FIB-EXAMPLE-EXECUTES", "G17-SDK-HELLO-EXAMPLE-EXECUTES", "G17-RUNTIME-TABLE-VERSIONED",
    "G17-RUNTIME-EOSVM-EXECUTES", "G17-RUNTIME-PYTHON-PENDING-INERT", "G17-RUNTIME-NON-GUEST-AND-UNKNOWN-REFUSED",
    "G17-EDITOR-CONTROL-BYTES-REJECTED-ATOMICALLY", "G17-EDITOR-UNDO-JOURNAL-BOUNDED", "G17-EDITOR-SAVE",
    "G17-EDITOR-LOAD-ROUNDTRIP", "G17-EDITOR-EDIT-ASSEMBLE-RUN-WORKFLOW",
    "G17-PKG-ANY-SINGLE-BIT-FLIP-REJECTED", "G17-PKG-FORGED-MANIFESTS-REJECTED-DESPITE-VALID-DIGEST",
    "G17-PKG-SIGN-VERIFY-AND-WRONG-KEY", "G17-MGR-IMPORT-NEEDS-PROVISIONED-KEY",
    "G17-MGR-INSTALL-REQUIRES-RECIPIENT-APPROVAL", "G17-MGR-APPROVAL-BOUND-TO-DIGEST-AND-EXPIRY",
    "G17-MGR-APPROVAL-IS-SINGLE-USE", "G17-MGR-INSTALLED-PROGRAM-EXECUTES",
    "G17-MGR-DOWNGRADE-REFUSED-WITHOUT-ROLLBACK-POLICY", "G17-MGR-ROLLBACK-WITH-POLICY-AND-APPROVAL",
    "G17-MGR-MISSING-DEPENDENCY-REFUSED", "G17-MGR-OFFLINE-EXPORT", "G17-MGR-OFFLINE-IMPORT-ON-SECOND-NODE",
    "G17-MGR-INSTALL-FAILURE-LEAVES-NO-PARTIAL-RECORD", "G17-MGR-CORRUPT-STATE-FAILS-CLOSED",
    "G17-MGR-TAMPERED-INSTALLED-PAYLOAD-NOT-EXECUTED",
    "G17-INT-MCP-INSPECTS-INTEGRITY", "G17-INT-MCP-COMPATIBILITY-INSPECTION", "G17-INT-MCP-PROPOSES-WITHOUT-EXECUTING",
    "G17-INT-MODEL-CANNOT-SELF-APPROVE", "G17-INT-INSTALL-REFUSED-WITHOUT-RECIPIENT-APPROVAL",
    "G17-INT-NO-EXECUTE-TOOL-EXISTS", "G17-INT-TOOL-SET-IS-THE-FROZEN-TEN", "G17-INT-NOTHING-RAN-VIA-MCP",
    "G17-INT-TAMPER-AFTER-APPROVAL-REFUSED", "G17-INT-REAL-INSTALLED-PROGRAM-EXECUTES", "G17-INT-AUDIT-CHAIN-VALID",
    "G17-XCHECK-ASSEMBLERS-BYTE-IDENTICAL", "G17-XCHECK-PACKAGES-BYTE-IDENTICAL",
    "G17-XCHECK-PYTHON-PACKAGE-IMPORTS", "G17-XCHECK-PYTHON-PACKAGE-EXECUTES",
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


def main() -> int:
    output = run_command(["sh", "scripts/test-devtools-host.sh"])
    print(output)
    check("FAIL G17" not in output, "G17-NO-FAILURES", "host tests reported no failures")
    check(output.count("G17 DEV RESULT: PASS") == 2, "G17-DEV-HOST", "dev tests pass in plain and ASan/UBSan builds")
    check(output.count("G17 INTEGRATION RESULT: PASS") == 2, "G17-INTEGRATION-HOST",
          "MCP integration passes in plain and ASan/UBSan builds")
    check(output.count("G17 XCHECK RESULT: PASS") == 2, "G17-XCHECK-HOST",
          "Python/C cross-check passes in plain and ASan/UBSan builds")
    for marker in ("G17-DEV-SANITIZERS", "G17-INTEGRATION-SANITIZERS", "G17-XCHECK-SANITIZERS", "G17-PYTHON-TOOL-BUILDS-ARTIFACTS"):
        check(f"PASS {marker}" in output, marker, "required stage passed")
    for test_id in REQUIRED:
        check(f"PASS {test_id}\n" in output + "\n", test_id, "required acceptance check present and passing")

    native = "PASS G17-NATIVE-AARCH64-BUILD-AND-COMPAT-INSPECTION" in output
    print("G17 native AArch64 build + ELF inspection: " + ("EXECUTED_PASS" if native else "NOT_RUN (toolchain unavailable)"))

    forbidden = re.compile(r"\b(system|exec[lv]p?e?|popen|fopen|open|fork|ioctl|syscall|socket|connect|bind|listen)\s*\(")
    for name in ("eosvm.c", "editor.c", "runtime_iface.c", "devpkg.c"):
        source = (ROOT / "kernel" / name).read_text(encoding="utf-8")
        check(not forbidden.search(source), f"G17-NO-AUTHORITY-{name}", f"{name} has no process, file, socket or hardware path")

    deploy = (ROOT / "kernel" / "mcp_deploy.c").read_text(encoding="utf-8")
    check("devpkg" not in deploy and "eosvm" not in deploy, "G17-MCP-SERVICE-HAS-NO-EXECUTION-PATH",
          "the MCP deployment service cannot reach the package manager or VM")
    check(len(re.findall(r"experimentalos_\w+_v1", (ROOT / "kernel" / "mcp_deploy.c").read_text(encoding="utf-8"))) >= 10,
          "G17-MCP-CONTRACTS-PRESENT", "frozen G14 tool contracts are still defined")

    progress = (ROOT / "docs" / "G17_PROGRESS.md").read_text(encoding="utf-8")
    for needle in ("Not implemented", "HARDWARE_NOT_VERIFIED", "compatibility-pending"):
        check(needle in progress, f"G17-DOC-{needle.upper().replace(' ', '-')}", f"progress record states '{needle}'")
    print("G17 software acceptance checks complete; hardware remains HARDWARE_NOT_VERIFIED.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL G17: {exc}", file=sys.stderr)
        raise SystemExit(1)
