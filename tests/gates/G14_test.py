#!/usr/bin/env python3
"""Validate the MCP framework and Development & Deployment Manager contracts (host-controlled)."""

from __future__ import annotations

import os
import re
import shlex
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

REQUIRED = [
    "G14-SHA256-ABC", "G14-SHA256-MILLION-A", "G14-JRPC-VALID-REQUEST", "G14-JRPC-BATCH-REJECTED",
    "G14-JRPC-DUPLICATE-KEY", "G14-REG-RUG-PULL-REJECTED", "G14-REG-AUDIT-TAMPER-DETECTED",
    "G14-REG-AUDIT-FULL-FAILS-CLOSED", "G14-LIFE-HANDSHAKE", "G14-LIFE-VERSION-NEGOTIATED",
    "G14-SCHEMA-FROZEN-CONTRACT-FINGERPRINTS", "G14-SCHEMA-CHANGED-TOOL-CALL-DENIED",
    "G14-UNTRUSTED-OUTPUT-STAYS-VALID-JSON", "G14-UNTRUSTED-NO-FOLLOW-ON-ACTIONS",
    "G14-AUTH-MISSING-CAPABILITY-DENIED", "G14-AUTH-SIDE-EFFECT-NEEDS-OWNER",
    "G14-QUOTA-CALL-LIMIT-ENFORCED", "G14-TIMEOUT-SERVER-ENFORCED", "G14-TIMEOUT-NO-SIDE-EFFECT",
    "G14-TIMEOUT-CLIENT-ENFORCED", "G14-CANCEL-NO-SIDE-EFFECT", "G14-RECOVERY-RECONNECT",
    "G14-RECOVERY-CLOSES-ABUSIVE-CONNECTION", "G14-DEPLOY-MODEL-APPROVAL-REJECTED",
    "G14-DEPLOY-BUILD-HAS-NO-SIGNING-AUTHORITY", "G14-DEPLOY-TAMPERING-DETECTED",
    "G14-DEPLOY-INSTALL-FAILURE-LEAVES-NODE-CLEAN", "G14-DEPLOY-INTERRUPTION-LEAVES-NODE-UNTOUCHED",
    "G14-DEPLOY-AUTHORIZED-ACTIVATION-RESUMED", "G14-DEPLOY-ROLLBACK-NEEDS-RECIPIENT-POLICY",
    "G14-DEPLOY-AUTHORIZED-ROLLBACK", "G14-DEPLOY-DEPENDENCY-MISMATCH-REFUSED",
    "G14-DEPLOY-MALFORMED-METADATA-REJECTED", "G14-DEPLOY-AUDIT-CHAIN-VALID",
]


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
    output = run_command(["sh", "scripts/test-mcp-host.sh"])
    print(output)
    check("FAIL G14" not in output, "G14-NO-FAILURES", "host tests reported no failures")
    check(output.count("G14 MCP HOST TESTS PASS") == 2 and output.count("G14 MCP INTEGRATION TESTS PASS") == 2,
          "G14-HOST", "unit and integration tests pass in plain and ASan/UBSan builds")
    check("PASS G14-MCP-SANITIZERS:" in output and "PASS G14-MCP-INTEGRATION-SANITIZERS:" in output,
          "G14-SANITIZERS", "MCP framework passes ASan and UBSan")
    for test_id in REQUIRED:
        check(f"PASS {test_id}\n" in output + "\n", test_id, "required acceptance check present and passing")

    contracts = (ROOT / "kernel" / "mcp_deploy.c").read_text(encoding="utf-8")
    names = re.findall(r'\{ "(experimentalos_[a-z0-9_]+_v1)"', contracts)
    check(len(names) == 10 and len(set(names)) == 10, "G14-TEN-CONTRACTS",
          "ten distinct versioned Development & Deployment tools are defined")
    check(not re.search(r"shell|exec|sudo", " ".join(names)), "G14-NO-SHELL-TOOL",
          "no shell or arbitrary execution tool is exposed")

    handlers = contracts[contracts.index("HANDLER(h_capabilities)"):contracts.index("int os_dm_register_tools")]
    for privileged in ("os_dm_activate", "os_dm_recipient_approve", "os_dm_sign_package",
                       "os_dm_provision_key", "os_dm_set_rollback_policy", "os_dm_enroll_node"):
        check(privileged not in handlers, f"G14-BOUNDARY-{privileged}",
              f"{privileged} is not reachable from any MCP tool handler")

    forbidden = re.compile(r"\b(system|exec[lv]p?e?|popen|fopen|open|fork|ioctl|syscall)\s*\(")
    for name in ("mcp_server.c", "mcp_client.c", "mcp_deploy.c", "mcp_registry.c", "jsonrpc.c", "sha256.c"):
        source = (ROOT / "kernel" / name).read_text(encoding="utf-8")
        check(not forbidden.search(source), f"G14-NO-AUTHORITY-{name}",
              f"{name} has no command, file or hardware execution path")

    progress = (ROOT / "docs" / "G14_PROGRESS.md").read_text(encoding="utf-8")
    check("Not implemented" in progress and "HARDWARE_NOT_VERIFIED" in progress,
          "G14-LIMITS-RECORDED", "progress record lists unimplemented scope and unverified hardware")
    print("G14 software acceptance checks complete; hardware remains HARDWARE_NOT_VERIFIED.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL G14: {exc}", file=sys.stderr)
        raise SystemExit(1)
