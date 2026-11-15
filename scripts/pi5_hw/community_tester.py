#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RROS contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline community evidence entry point. Physical serial capture is blocked."""
from __future__ import annotations

import sys
from pathlib import Path


def main() -> int:
    if any(arg == "--port" or arg.startswith("--port=") for arg in sys.argv[1:]):
        print("BLOCKED: physical serial connection/capture has no approved safety gate. "
              "Use --log for offline import or demo for synthetic evidence.", file=sys.stderr)
        return 2
    here = Path(__file__).resolve().parent
    if not (here / "tester.py").is_file():
        source = here.parent.parent / "tests" / "pi5_hw_validation"
        if source.is_dir():
            sys.path.insert(0, str(source))
    from tester import main as run
    return run()


if __name__ == "__main__":
    raise SystemExit(main())
