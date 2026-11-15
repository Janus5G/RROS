#!/usr/bin/env python3
"""Reproducible G00 reference, environment and toolchain acceptance checks."""

from __future__ import annotations

import os
import json
import re
import shlex
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
def check(condition: bool, test_id: str, message: str) -> None:
    if not condition:
        raise AssertionError(f"FAIL {test_id}: {message}")
    print(f"PASS {test_id}: {message}")


def run(command: list[str], *, cwd: Path | None = None) -> str:
    result = subprocess.run(
        command, cwd=cwd, text=True, encoding="utf-8", stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, check=False,
    )
    if result.returncode:
        raise RuntimeError(
            f"Command failed ({result.returncode}): {command!r}\n{result.stdout}"
        )
    return result.stdout.strip()


def main() -> int:
    master = ROOT / "MASTER_PROMPT.md"
    pdf = ROOT / "OSBuild.pdf"
    audit = ROOT / "docs" / "REFERENCE_ARCHITECTURE_AUDIT.md"
    toolchain_doc = ROOT / "docs" / "TOOLCHAIN.md"
    state_path = ROOT / "build" / "gate_state.json"
    for path in (master, pdf, audit, toolchain_doc, state_path):
        check(path.is_file() and path.stat().st_size > 0, "G00-ARTIFACT", str(path.relative_to(ROOT)))

    prompt_text = master.read_text(encoding="utf-8")
    audit_text = audit.read_text(encoding="utf-8")
    check(len(prompt_text.splitlines()) >= 1700, "G00-SPEC", "master specification is complete")
    check(pdf.read_bytes()[:8] == b"%PDF-1.4", "G00-PDF", "reference is a PDF 1.4 document")
    page_count = len(re.findall(rb"/Type\s*/Page\b", pdf.read_bytes()))
    check(page_count == 34, "G00-PDF-PAGES", f"reference contains {page_count} pages")
    mapped_sections = re.findall(r"^\|\s*(\d+)\.", audit_text, flags=re.MULTILINE)
    check(
        mapped_sections == [str(number) for number in range(1, 23)],
        "G00-REFERENCE-COVERAGE",
        "all 22 reference sections have individual audit rows",
    )
    check(
        "b0dc46f860faedb6f00a36b2c636a2b31c2b290e" in audit_text
        and "ROBOFORGE_MCP_ENABLED=1" in audit_text,
        "G00-ROBOFORGE-PROVENANCE",
        "read-only MCP source revision and opt-in boundary are recorded",
    )
    state = json.loads(state_path.read_text(encoding="utf-8"))
    check(
        state.get("schema_version") == 1
        and list(state["gates"]) == [f"G{number:02d}" for number in range(23)],
        "G00-STATE",
        "resumable gate state has a versioned entry for G00-G22",
    )
    runner_test = run([sys.executable, str(ROOT / "tests" / "unit" / "test_gate_runner.py")])
    print(runner_test)
    check(
        "PASS G00-GATE-RUNNER" in runner_test,
        "G00-GATE-RUNNER",
        "UTF-8 diagnostics and duplicate history handling are tested",
    )

    if os.name == "nt":
        wsl_root = run(["wsl", "--exec", "wslpath", "-a", str(ROOT)])
        temp_host = Path(tempfile.mkdtemp(prefix="experimentalos-g00-"))
        temp_wsl = run(["wsl", "--exec", "wslpath", "-a", str(temp_host)])
        prefix = (
            f"root={shlex.quote(wsl_root)}; "
            "tool=\"$root/.toolchain/llvm\"; "
            "export PATH=\"$tool/usr/lib/llvm-21/bin:$PATH\"; "
            "export LD_LIBRARY_PATH=\"$tool/usr/lib/x86_64-linux-gnu:"
            "$tool/usr/lib/llvm-21/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}\"; "
        )
        command_prefix = ["wsl", "--exec", "sh", "-lc"]
    else:
        wsl_root = str(ROOT)
        temp_host = Path(tempfile.mkdtemp(prefix="experimentalos-g00-"))
        temp_wsl = str(temp_host)
        prefix = (
            f"root={shlex.quote(wsl_root)}; "
            "tool=\"$root/.toolchain/llvm\"; "
            "export PATH=\"$tool/usr/lib/llvm-21/bin:$PATH\"; "
            "export LD_LIBRARY_PATH=\"$tool/usr/lib/x86_64-linux-gnu:"
            "$tool/usr/lib/llvm-21/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}\"; "
        )
        command_prefix = ["sh", "-lc"]

    try:
        versions = run(
            command_prefix + [
                prefix
                + "clang --version | head -n 1; "
                + "ld.lld --version | head -n 1; "
                + "qemu-system-x86_64 --version | head -n 1; "
                + "python3 --version; "
                + "test -r /usr/share/OVMF/OVMF_CODE_4M.fd "
                + "&& echo 'OVMF_CODE_4M.fd available'"
            ]
        )
        for line in versions.splitlines():
            print(f"TOOLCHAIN: {line}")
        check("21.1.8" in versions, "G00-LLVM", "pinned Clang and LLD execute")
        check("QEMU emulator version 10.2.1" in versions, "G00-QEMU", "x86-64 QEMU executes")
        check("OVMF_CODE_4M.fd available" in versions, "G00-OVMF", "UEFI firmware is available")

        source = temp_host / "probe.c"
        image = temp_host / "probe.efi"
        source.write_text("void efi_main(void) {}\n", encoding="ascii")
        source_wsl = run(["wsl", "--exec", "wslpath", "-a", str(source)]) if os.name == "nt" else str(source)
        image_wsl = run(["wsl", "--exec", "wslpath", "-a", str(image)]) if os.name == "nt" else str(image)
        build = (
            prefix
            + f"clang --target=x86_64-pc-windows-msvc -ffreestanding -fshort-wchar "
            + f"-mno-red-zone -fno-stack-protector -c {shlex.quote(source_wsl)} "
            + f"-o {shlex.quote(temp_wsl + '/probe.obj')} && "
            + f"ld.lld -flavor link /subsystem:efi_application /entry:efi_main "
            + f"/nodefaultlib /machine:x64 /out:{shlex.quote(image_wsl)} "
            + shlex.quote(temp_wsl + "/probe.obj")
        )
        output = run(command_prefix + [build])
        check(image.is_file(), "G00-EFI-LINK", "freestanding x86-64 EFI image linked")
        data = image.read_bytes()
        pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
        check(data[:2] == b"MZ" and data[pe_offset:pe_offset + 4] == b"PE\0\0",
              "G00-PE", "linked artifact has valid PE/COFF headers")
        subsystem = struct.unpack_from("<H", data, pe_offset + 24 + 68)[0]
        check(subsystem == 10, "G00-EFI-SUBSYSTEM", "PE subsystem is EFI application (10)")
        check("PE32+ executable for EFI" in output or image.stat().st_size >= 512,
              "G00-TOOLCHAIN-OUTPUT", "toolchain emitted a non-empty EFI binary")
    finally:
        for item in temp_host.iterdir():
            item.unlink()
        temp_host.rmdir()

    print("G00 acceptance checks complete.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL G00: {exc}", file=sys.stderr)
        raise SystemExit(1)
