#!/usr/bin/env python3
"""Validate release Python syntax, local Markdown targets and Actions definitions."""
from __future__ import annotations

import ast
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from urllib.parse import unquote, urlparse

ROOT = Path(__file__).resolve().parents[2]
DOCUMENTS = (
    "README.md", "README.da.md", "CONTRIBUTING.md", "THIRD_PARTY_NOTICES.md",
    "HARDWARE_VALIDATION.md", "HARDWARE_COMPATIBILITY_MATRIX.md",
    "RASPBERRY_PI5_TEST_GUIDE.md", "RASPBERRY_PI5_TEST_GUIDE.da.md",
    "COMMUNITY_TEST_REPORT_TEMPLATE.md", "docs/release/RELEASE_NOTES.md",
    "docs/release/TESTING.md", "docs/release/PUBLISHING.md",
)


def main() -> int:
    for directory in ("scripts", "tests", "tools"):
        for path in (ROOT / directory).rglob("*.py"):
            ast.parse(path.read_text(encoding="utf-8-sig"), filename=str(path))
    print("PASS release Python syntax")
    for relative in DOCUMENTS:
        path = ROOT / relative
        for target in re.findall(r"!?\[[^\]]*\]\(([^)]+)\)", path.read_text(encoding="utf-8")):
            parsed = urlparse(target)
            if parsed.scheme or target.startswith("#"):
                continue
            resolved = (path.parent / unquote(parsed.path)).resolve()
            if not resolved.exists():
                raise FileNotFoundError(f"{relative}: missing link target {target}")
    print("PASS public documentation local links")
    ET.parse(ROOT / "docs/assets/rros-banner.svg")
    print("PASS original SVG branding syntax")
    actionlint = Path(os.environ.get("RROS_ACTIONLINT", ROOT / ".toolchain/actionlint/actionlint"))
    if not actionlint.is_file():
        raise FileNotFoundError("actionlint missing: run sh scripts/release/bootstrap-actionlint.sh")
    # Deterministic Actions validation; shellcheck is separately optional, not an implied PASS.
    subprocess.run([str(actionlint), "-shellcheck=", "-pyflakes=",
                    *[str(p) for p in sorted((ROOT / ".github/workflows").glob("*.yml"))]],
                   cwd=ROOT, check=True)
    print("PASS GitHub Actions definitions (actionlint; no shellcheck/pyflakes claim)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
