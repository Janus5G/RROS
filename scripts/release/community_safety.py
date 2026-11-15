#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RROS contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Targeted compiler analysis only; never approves physical operations."""
from __future__ import annotations

import json
import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
    directory = ROOT / "build" / "community-checks"
    directory.mkdir(parents=True, exist_ok=True)
    llvm = ROOT / ".toolchain" / "llvm" / "usr" / "lib" / "llvm-21"
    env = {**os.environ, "LD_LIBRARY_PATH": str(llvm.parent.parent / "lib" / "x86_64-linux-gnu")
           + ":" + str(llvm / "lib") + ":" + os.environ.get("LD_LIBRARY_PATH", "")}
    compiler = llvm / "bin" / "clang"
    results = []
    targets = [(name, None) for name in (
        "kernel/memory.c", "boot/aarch64/pi5_probe_support.c",
        "boot/aarch64/pi5_probe_main.c")]
    targets += [("kernel/pi5_driver.c", function) for function in (
        "dma_page_pool_allocate", "dma_page_pool_release",
        "aarch64_dma_cache_range", "aarch64_dma_cache_maintain",
        "rp1_audio_out_dma_buffer_owned")]
    for source, function in targets:
        command = [str(compiler), "--analyze", "--target=aarch64-pc-windows-msvc",
                   "-std=c17", "-Wall", "-Wextra", "-Werror", "-ffreestanding",
                   "-fshort-wchar", "-I", str(ROOT / "kernel"), "-I", str(ROOT / "boot" / "uefi"),
                   "-Xanalyzer", "-analyzer-output=text",
                   "-Xanalyzer", "-analyzer-display-progress", str(ROOT / source)]
        if function is not None:
            command[1:1] = [
                "-Xanalyzer", "-analyze-function",
                "-Xanalyzer", function]
        completed = subprocess.run(command, cwd=directory, env=env,
                                   capture_output=True, text=True, check=False)
        # Clang's analyzer can return zero while emitting findings.
        output = completed.stdout + completed.stderr
        findings = any(marker in output
                       for marker in ("warning:", "error:"))
        analyzed = "ANALYZE (Path" in output
        status = "PASS" if completed.returncode == 0 and not findings and analyzed else "FAIL"
        results.append({"source": source, "function": function, "command": command,
                        "path_analysis_executed": analyzed,
                        "status": status, "exit_code": completed.returncode,
                        "output": output})
        print(status, source, function or "(translation unit)", flush=True)
    report = {"scope": "Three complete translation units and five DMA ownership/cache functions; "
                       "Clang static analyzer, not exhaustive or physical",
              "hardware_verification": "NOT_VERIFIED", "physical_operations_approved": [],
              "results": results}
    (directory / "static-analysis.json").write_text(json.dumps(report, indent=2) + "\n",
                                                   encoding="utf-8")
    return 0 if all(r["status"] == "PASS" for r in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
