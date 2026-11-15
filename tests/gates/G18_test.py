#!/usr/bin/env python3
"""Validate the browser and shared application services (host-tested with fixture transport only)."""

from __future__ import annotations

import os
import re
import shlex
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MODULES = ("browser_net.c", "browser_html.c", "browser_js.c", "browser_image.c", "browser.c", "docsvc.c")
KEY_IDS = [
    "G18-URL-REJECTS-UNSUPPORTED-SCHEMES-AND-EMPTY-INPUT", "G18-NET-DENIES-EVERYTHING-BY-DEFAULT",
    "G18-NET-HTTPS-REFUSED-WITHOUT-TLS", "G18-NET-REDIRECT-TO-DISALLOWED-HOST-DENIED",
    "G18-HTTP-CHUNKED-AND-COMPRESSED-REFUSED", "G18-HTML-INLINE-HANDLERS-COUNTED-AND-NEVER-COLLECTED",
    "G18-HTML-UNTERMINATED-CONSTRUCTS-FAIL-CLOSED", "G18-JS-FUEL-BOUNDS-INFINITE-LOOPS",
    "G18-JS-UNSUPPORTED-AND-DANGEROUS-CONSTRUCTS-REJECTED-WITHOUT-EFFECT", "G18-IMG-PNG-JPEG-GIF-AND-UNKNOWN-REFUSED-NO-DECOMPRESSOR",
    "G18-BROWSER-FAILED-NAVIGATION-IS-ATOMIC", "G18-BROWSER-SCRIPTS-HAVE-NO-NETWORK-ACCESS",
    "G18-PRINT-REFUSED-WITHOUT-OWNER-APPROVAL", "G18-PRINT-APPROVAL-BOUND-TO-JOB-DIGEST",
    "G18-PRINT-APPROVAL-IS-SINGLE-USE-REPRINT-REFUSED", "G18-PRINT-HARDWARE-BACKEND-FAILS-CLOSED-NO-DRIVER",
    "G18-DOC-SAVE-AND-LOAD-ROUNDTRIP",
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
    output = run_command(["sh", "scripts/test-browser-host.sh"])
    print(output)
    check("FAIL G18" not in output, "G18-NO-FAILURES", "host tests reported no failures")
    check(output.count("G18 BROWSER RESULT: PASS") == 2, "G18-BROWSER-HOST", "browser tests pass in plain and ASan/UBSan builds")
    check("PASS G18-BROWSER-SANITIZERS" in output, "G18-BROWSER-SANITIZERS", "sanitizer stage passed")
    source = (ROOT / "tests" / "gates" / "G18_browser_test.c").read_text(encoding="utf-8")
    ids = sorted(set(re.findall(r'"(G18-[A-Z0-9-]+)"\)', source)))
    check(len(ids) >= 150, "G18-CHECK-COUNT", f"{len(ids)} distinct acceptance checks defined")
    for test_id in ids:
        check((f"PASS {test_id}\n" in output + "\n") and output.count(f"PASS {test_id}\n") == 2, test_id,
              "check present and passing in both builds")
    for test_id in KEY_IDS:
        check(test_id in ids, f"KEY-{test_id}", "key safety check is defined")

    forbidden = re.compile(r"\b(system|exec[lv]p?e?|popen|fopen|open|fork|ioctl|syscall|socket|connect|bind|listen)\s*\(")
    for name in MODULES:
        text = (ROOT / "kernel" / name).read_text(encoding="utf-8")
        check(not forbidden.search(text), f"G18-NO-AUTHORITY-{name}", f"{name} has no process, file, socket or hardware path")
    js = (ROOT / "kernel" / "browser_js.c").read_text(encoding="utf-8")
    check("browser_net" not in js and "os_net" not in js, "G18-JS-ENGINE-CANNOT-REACH-NETWORK", "script engine has no network dependency")

    progress = (ROOT / "docs" / "G18_PROGRESS.md").read_text(encoding="utf-8")
    for needle in ("Not implemented", "HARDWARE_NOT_VERIFIED", "compatibility-pending"):
        check(needle in progress, f"G18-DOC-{needle.upper().replace(' ', '-')}", f"progress record states '{needle}'")
    print("G18 software acceptance checks complete; hardware remains HARDWARE_NOT_VERIFIED.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL G18: {exc}", file=sys.stderr)
        raise SystemExit(1)
