#!/usr/bin/env python3
"""Validate G20 security: accounts, roles, quotas, audit, signed/encrypted three-node deployment (simulation)."""

from __future__ import annotations

import os
import re
import shlex
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
KEY_IDS = [
    "G20-CHACHA20-RFC8439-KNOWN-ANSWER",
    "G20-PBKDF2-SHA256-KNOWN-ANSWER-1",
    "G20-PLAINTEXT-PASSWORDS-NEVER-STORED",
    "G20-ADMIN-CANNOT-CREATE-ADMIN",
    "G20-LOCKOUT-AFTER-REPEATED-FAILURES",
    "G20-SERVICE-RESPONSE-REPLAY-REFUSED",
    "G20-AUDIT-TAMPER-DETECTED",
    "G20-TRANSFER-IS-ENCRYPTED",
    "G20-REPLAYED-FRAME-REFUSED",
    "G20-IMPERSONATION-WITH-WRONG-PEER-KEY-REFUSED",
    "G20-MALFORMED-MANIFEST-JSON-REFUSED-EVEN-WHEN-SIGNED",
    "G20-PACKAGE-TAMPER-DETECTED-BY-SIGNATURE",
    "G20-INVALID-SIGNATURE-REFUSED",
    "G20-INCOMPATIBLE-ARCHITECTURE-REFUSED",
    "G20-MISSING-DEPENDENCY-REFUSED",
    "G20-USER-CANNOT-RECEIVE-PACKAGES",
    "G20-CRASH-MID-INSTALL-LEAVES-JOURNAL",
    "G20-FAILED-INSTALLATION-ROLLS-BACK-AUTOMATICALLY",
    "G20-ROLLBACK-RESTORES-PREVIOUS-VERSION",
    "G20-DOWNGRADE-ATTACK-REFUSED",
    "G20-AUTHORIZED-ACTIVATION",
    "G20-REVOCATION-BLOCKS-ALREADY-STAGED-AND-APPROVED-INSTALL",
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
    output = run_command(["sh", "scripts/test-security-host.sh"])
    print(output)
    check("FAIL G20" not in output, "G20-NO-FAILURES", "host tests reported no failures")
    check(output.count("G20 SECURITY RESULT: PASS") == 2, "G20-SECURITY-HOST",
          "security tests pass in plain and ASan/UBSan builds")
    source = (ROOT / "tests" / "gates" / "G20_security_test.c").read_text(encoding="utf-8")
    ids = sorted(set(re.findall(r'"(G20-[A-Z0-9-]+)"\)', source)))
    check(len(ids) >= 150, "G20-CHECK-COUNT", f"{len(ids)} distinct acceptance checks defined")
    for test_id in ids:
        check(len(re.findall(rf"^PASS {re.escape(test_id)}$", output, re.M)) == 2, test_id,
              "check present and passing in both builds")
    for test_id in KEY_IDS:
        check(test_id in ids, f"KEY-{test_id}", "key safety check is defined")

    forbidden = re.compile(r"\b(system|exec[lv]p?e?|popen|fopen|open|fork|ioctl|syscall|socket|connect|bind|listen)\s*\(")
    for name in ("chacha20.c", "secsvc.c", "secdeploy.c"):
        text = (ROOT / "kernel" / name).read_text(encoding="utf-8")
        check(not forbidden.search(text), f"G20-NO-AUTHORITY-{name}", f"{name} has no process, file, socket or hardware path")

    progress = (ROOT / "docs" / "G20_PROGRESS.md").read_text(encoding="utf-8")
    for needle in ("Not implemented", "HARDWARE_NOT_VERIFIED", "compatibility-pending"):
        check(needle in progress, f"G20-DOC-{needle.upper().replace(' ', '-')}", f"progress record states '{needle}'")
    print("G20 software acceptance checks complete; hardware remains HARDWARE_NOT_VERIFIED.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL G20: {exc}", file=sys.stderr)
        raise SystemExit(1)
