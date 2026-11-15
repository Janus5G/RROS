#!/usr/bin/env python3
"""Validate the ChromaNeural, PRISME and ChromaPlex compatibility adapters (host-controlled)."""

from __future__ import annotations

import os
import re
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CN_PINNED = "3ce209ccd2aeb52094fbaade4429632eb17d3340"
CN_CHECKOUT = Path(os.environ.get("CHROMANEURAL_SRC", ROOT / "build" / "external" / "ChromaNeural"))

REQUIRED = [
    "G16-HMAC-RFC4231-VECTORS", "G16-HMAC-TAG-COMPARE", "G16-CNRB-MATCHES-UPSTREAM-BYTE-FOR-BYTE",
    "G16-IDENTITY-MATCHES-UPSTREAM-DERIVATION", "G16-IDENTITY-MISMATCH-AND-NONCANONICAL-REJECTED",
    "G16-IDENTITY-DOCUMENT-ACCEPTED", "G16-IDENTITY-BAD-DOCUMENT-REJECTED",
    "G16-CNRB-SMALL-CONTAINER-BUILT", "G16-CNRB-ANY-SINGLE-BIT-FLIP-REJECTED",
    "G16-CNRB-TRAILING-AND-TRUNCATED-REJECTED", "G16-CNRB-WRONG-KEY-REJECTED",
    "G16-CNRB-AUTHENTICATED-BAD-METADATA-REJECTED", "G16-CNRB-FORMAT-AND-SIZE-BOUNDS-ENFORCED",
    "G16-CNRB-OVERSIZED-HEADER-LENGTH-REJECTED", "G16-RECEIVER-RESUMABLE-AND-VERIFIED",
    "G16-RECEIVER-CHUNK-LIMIT-ENFORCED", "G16-RECEIVER-DIGEST-MISMATCH-AND-BAD-INIT-REJECTED",
    "G16-MCP-LIMIT-PROFILE-ENFORCED", "G16-REGISTRY-LEAST-AUTHORITY-AND-VERSIONED",
    "G16-REGISTRY-VERIFIED-ADAPTERS-USABLE", "G16-REGISTRY-PENDING-ADAPTERS-INERT",
    "G16-REGISTRY-PENDING-STATUS-RECORDED-WITHOUT-AUTHORITY", "G16-REGISTRY-PRISME-LICENSE-RECORDED",
    "G16-REGISTRY-BOUNDS", "G16-PRISME-STRUCTURE-4X7-UV-CONTROL-ONLY",
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


def upstream_checks() -> str:
    head = subprocess.run(["git", "-C", str(CN_CHECKOUT), "rev-parse", "HEAD"], text=True,
                          capture_output=True).stdout.strip()
    check(head == CN_PINNED, "G16-UPSTREAM-PINNED-REVISION", "ChromaNeural checkout is the pinned revision")
    transport = (CN_CHECKOUT / "client" / "chroma" / "transport.py").read_text(encoding="utf-8")
    check("MAX=1024*1024" in transport and "CHUNK=32768" in transport and '">4sBII"' in transport and
          '("raw","cpl","cpa","prisme-asm","optb")' in transport,
          "G16-UPSTREAM-CNRB-CONSTANTS", "upstream CNRB limits, header and format list match the adapter")
    client = (CN_CHECKOUT / "client" / "chroma" / "mcp_client.py").read_text(encoding="utf-8")
    check("> 8192" in client and "> 32768" in client and "len(identifiers) > 4" in client and "len(found) > 16" in client,
          "G16-UPSTREAM-MCP-LIMITS", "upstream MCP argument, result, server and tool limits match the adapter")
    check('"2025-03-26", \'2025-06-18\'' in client.replace('"2025-03-26"', '"2025-03-26"') or "'2025-11-25'" in client,
          "G16-UPSTREAM-MCP-PROTOCOLS", "upstream accepts the same MCP protocol versions")
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "vectors.txt"
        subprocess.run([sys.executable, str(ROOT / "scripts" / "g16_generate_vectors.py"), str(CN_CHECKOUT), str(out)],
                       check=True)
        committed = (ROOT / "tests" / "gates" / "fixtures" / "chromaneural" / "vectors.txt").read_text(encoding="ascii")
        check(out.read_text(encoding="ascii") == committed, "G16-UPSTREAM-VECTORS-REPRODUCE",
              "committed vectors are reproduced by running the pinned upstream transport.py")
    return "EXECUTED_PASS"


def main() -> int:
    output = run_command(["sh", "scripts/test-compat-host.sh"])
    print(output)
    check("FAIL G16" not in output, "G16-NO-FAILURES", "host tests reported no failures")
    check(output.count("G16 RESULT: PASS") == 2, "G16-HOST", "adapter tests pass in plain and ASan/UBSan builds")
    check("PASS G16-COMPAT-SANITIZERS:" in output, "G16-SANITIZERS", "adapters pass ASan and UBSan")
    for test_id in REQUIRED:
        check(f"PASS {test_id}\n" in output + "\n", test_id, "required acceptance check present and passing")

    forbidden = re.compile(r"\b(system|exec[lv]p?e?|popen|fopen|open|fork|ioctl|syscall|socket|connect|bind|listen)\s*\(")
    for name in ("chroma_adapter.c", "compat_registry.c", "hmac_sha256.c"):
        source = (ROOT / "kernel" / name).read_text(encoding="utf-8")
        check(not forbidden.search(source), f"G16-NO-AUTHORITY-{name}",
              f"{name} has no process, file, socket or hardware path")
    others = " ".join(p.read_text(encoding="utf-8", errors="ignore")
                      for p in (ROOT / "kernel").glob("*.c")
                      if p.name not in ("chroma_adapter.c", "compat_registry.c"))
    check("chroma_adapter" not in others and "compat_registry" not in others, "G16-ADAPTERS-OPTIONAL",
          "no other kernel module depends on the optional adapters")

    live = "NOT_RUN"
    if (CN_CHECKOUT / "client" / "chroma" / "transport.py").exists():
        live = upstream_checks()
    else:
        print("WARNING G16-UPSTREAM: pinned ChromaNeural checkout unavailable; upstream cross-checks NOT RUN.")
    print(f"G16 upstream cross-check against pinned ChromaNeural source: {live} "
          "(no live ChromaNeural node, no network, no hardware)")

    progress = (ROOT / "docs" / "G16_PROGRESS.md").read_text(encoding="utf-8")
    for needle in ("Not implemented", "HARDWARE_NOT_VERIFIED", "COMPATIBILITY_PENDING", "All rights reserved"):
        check(needle in progress, f"G16-DOC-{needle.upper().replace(' ', '-')}", f"progress record states '{needle}'")
    print("G16 software acceptance checks complete; hardware remains HARDWARE_NOT_VERIFIED.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL G16: {exc}", file=sys.stderr)
        raise SystemExit(1)
