#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RROS contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Export committed, filtered RROS v1 source and reproducible review downloads."""
from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import os
import platform
import posixpath
import re
import subprocess
import sys
import tempfile
import textwrap
import zipfile
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[2]
VERSION = "1.0.0-dev.2"
PREFIX = f"RROS-{VERSION}"
EXCLUDED = ("tests/gates/fixtures/", "docs/site/", "docs/owner/", ".github/agents/")
DIRECTORIES = ("arch/", "boot/", "kernel/", "sdk/", "scripts/", "tests/",
               "tools/", "docs/", ".github/workflows/", ".github/ISSUE_TEMPLATE/",
               ".github/DISCUSSION_TEMPLATE/")
ROOT_FILES = {
    "README.md", "README.da.md", "LICENSE", "THIRD_PARTY_NOTICES.md",
    "CONTRIBUTING.md", "CODE_OF_CONDUCT.md", "SECURITY.md", ".gitattributes",
    ".gitignore", ".github/pull_request_template.md",
    "HARDWARE_COMPATIBILITY_MATRIX.md", "COMMUNITY_TEST_REPORT_TEMPLATE.md",
    "HARDWARE_VALIDATION.md", "RASPBERRY_PI5_TEST_GUIDE.md",
    "RASPBERRY_PI5_TEST_GUIDE.da.md",
}
BINARY_SUFFIXES = {".pdf", ".fd", ".dtb", ".iso", ".img", ".efi", ".obj",
                   ".exe", ".deb", ".zip", ".pyc", ".pyo", ".so", ".dll"}


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def json_bytes(value: object) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8")


def selected(name: str) -> bool:
    path = PurePosixPath(name)
    return (
        not name.startswith(EXCLUDED) and "__pycache__" not in path.parts
        and path.suffix.lower() not in BINARY_SUFFIXES
        and (name in ROOT_FILES or name.startswith(DIRECTORIES))
    )


def safe_name(name: str) -> None:
    path = PurePosixPath(name)
    if not name or path.is_absolute() or ".." in path.parts or "\\" in name or ":" in name:
        raise ValueError(f"Unsafe archive path: {name!r}")


def source(commit: str, source_tree: bool) -> tuple[dict[str, bytes], dict]:
    if source_tree:
        provenance = json.loads((ROOT / "SOURCE_PROVENANCE.json").read_bytes())
        expected = (ROOT / "PACKAGE_SHA256SUMS").read_text(encoding="ascii")
        files = {}
        for line in expected.splitlines():
            checksum, name = line.split("  ", 1)
            safe_name(name)
            path = ROOT / name
            if path.is_symlink() or not path.is_file():
                raise ValueError(f"Missing/nonregular source file: {name}")
            data = path.read_bytes()
            if digest(data) != checksum:
                raise ValueError(f"Source checksum mismatch: {name}")
            files[name] = data
        files.pop("SOURCE_PROVENANCE.json")
        if not re.fullmatch(r"[0-9a-f]{40}", provenance["source_commit"]):
            raise ValueError("Invalid source provenance commit")
        return files, provenance
    revision = subprocess.check_output(
        ["git", "rev-parse", "--verify", commit + "^{commit}"], cwd=ROOT, text=True).strip()
    tree = subprocess.check_output(["git", "ls-tree", "-rz", revision], cwd=ROOT)
    names = []
    for entry in tree.split(b"\0"):
        if not entry:
            continue
        metadata, raw_name = entry.split(b"\t", 1)
        name = raw_name.decode("utf-8")
        if not selected(name):
            continue
        safe_name(name)
        if metadata.split()[0] not in {b"100644", b"100755"}:
            raise ValueError(f"Source symlink/nonregular file refused: {name}")
        names.append(name)
    request = "".join(f"{revision}:{name}\n" for name in names).encode()
    response = subprocess.run(["git", "cat-file", "--batch"], cwd=ROOT,
                              input=request, capture_output=True, check=True)
    stream = io.BytesIO(response.stdout)
    files = {}
    for name in names:
        _, kind, size = stream.readline().split()
        if kind != b"blob":
            raise ValueError(f"Expected Git blob: {name}")
        files[name] = stream.read(int(size))
        if stream.read(1) != b"\n":
            raise ValueError("Invalid Git batch response")
    provenance = {
        "version": VERSION, "source_commit": revision, "source": "committed Git blobs",
        "excluded": list(EXCLUDED) + ["OSBuild.pdf", "build/", "logs/", ".toolchain/",
                                    "third-party binaries and firmware"],
        "hardware_verification": "NOT_VERIFIED",
        "source_date_epoch": int(subprocess.check_output(
            ["git", "show", "-s", "--format=%ct", revision], cwd=ROOT, text=True).strip()),
    }
    return files, provenance


def zip_bytes(files: dict[str, bytes]) -> bytes:
    content = dict(files)
    content["PACKAGE_SHA256SUMS"] = "".join(
        f"{digest(data)}  {name}\n" for name, data in sorted(files.items())
    ).encode("ascii")
    target = io.BytesIO()
    with zipfile.ZipFile(target, "w", compression=zipfile.ZIP_STORED) as archive:
        for name, data in sorted(content.items()):
            safe_name(name)
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            info.create_system = 3
            info.external_attr = (0o100755 if name.endswith(".sh") else 0o100644) << 16
            archive.writestr(info, data)
    return target.getvalue()


def standalone_notices(data: bytes, revision: str) -> bytes:
    def replace(match: re.Match[str]) -> str:
        label, target = match.groups()
        if target == "LICENSE" or target.startswith(("https://", "http://", "#")):
            return match.group()
        path = posixpath.normpath(target)
        safe_name(path.split("#", 1)[0])
        return f"[{label}](https://github.com/Janus5G/RROS/blob/{revision}/{path})"
    return re.sub(r"\[([^\]]+)\]\(([^)]+)\)", replace,
                  data.decode("utf-8")).encode("utf-8")


def verify_zip(data: bytes) -> None:
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        names = archive.namelist()
        if len(set(names)) != len(names):
            raise ValueError("Duplicate archive members")
        for name in names:
            safe_name(name)
        declared = {}
        for line in archive.read("PACKAGE_SHA256SUMS").decode("ascii").splitlines():
            checksum, name = line.split("  ", 1)
            if name in declared:
                raise ValueError("Duplicate checksum entry")
            declared[name] = checksum
        if set(declared) != set(names) - {"PACKAGE_SHA256SUMS"}:
            raise ValueError("Archive checksum coverage mismatch")
        for name, checksum in declared.items():
            if digest(archive.read(name)) != checksum:
                raise ValueError(f"Archive checksum mismatch: {name}")


def pdf_guide(markdown: str, revision: str, bom: str = "") -> bytes:
    """Original vector/text PDF using standard PDF fonts, no bundled font files."""
    pages: list[str] = []
    operations: list[str] = []
    y = 0

    def escaped(text: str) -> str:
        return text.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)")

    def text(value: str, x: int, baseline: int, size: int = 10, bold: bool = False) -> None:
        font = "F2" if bold else "F1"
        operations.append(f"BT /{font} {size} Tf 0.12 0.19 0.27 rg "
                          f"1 0 0 1 {x} {baseline} Tm ({escaped(value)}) Tj ET")

    def new_page() -> None:
        nonlocal operations, y
        if operations:
            pages.append("\n".join(operations))
        operations = ["0.05 0.09 0.16 rg 0 750 595 92 re f",
                      "0.44 0.89 0.79 rg 44 770 5 38 re f"]
        operations.append("BT /F2 19 Tf 0.95 0.97 1 rg 1 0 0 1 62 791 Tm "
                          "(RoboticRasOS / RROS) Tj ET")
        operations.append("BT /F1 10 Tf 0.7 0.82 0.9 rg 1 0 0 1 62 772 Tm "
                          "(V1 COMMUNITY / ENGINEERING REVIEW) Tj ET")
        text(f"{VERSION}  |  {revision[:12]}  |  NOT PHYSICAL TEST APPROVAL", 44, 28, 8)
        text(str(len(pages) + 1), 541, 28, 8)
        y = 714

    new_page()
    for line in markdown.splitlines():
        if line.startswith("## "):
            if y < 650:
                new_page()
            title = line[3:]
            for part in textwrap.wrap(title, 55):
                text(part, 44, y, 16, True)
                y -= 23
            y -= 12
            if title.startswith("2."):
                for item in csv.DictReader(io.StringIO(bom)):
                    label = f"{item['quantity']} x {item['item']}: {item['specification']}"
                    for part in textwrap.wrap(label, 93):
                        if y < 65:
                            new_page()
                        text(part, 44, y, 9)
                        y -= 13
                    y -= 6
                y -= 9
            if title.startswith("3."):
                for x, label in [(44, "PI 5"), (174, "POWER"), (304, "DEBUG"), (434, "HOST")]:
                    operations.append(f"0.9 0.95 0.96 rg {x} {y-57} 116 58 re f")
                    text(label, x + 12, y - 18, 12, True)
                    text("DISCONNECTED", x + 8, y - 38, 8)
                y -= 83
                text("Inspection map only: no connections, pinout or power-up sequence.", 44, y, 9)
                y -= 25
        elif line.startswith("# "):
            for part in textwrap.wrap(line[2:], 40):
                text(part, 44, y, 21, True)
                y -= 29
            text("Source commit: " + revision, 44, y - 5, 8)
            y -= 40
        else:
            clean = line.replace("**", "").replace("`", "")
            lines = textwrap.wrap(clean, 90, break_long_words=True) or [""]
            for part in lines:
                if y < 65:
                    new_page()
                text(part, 44, y, 10)
                y -= 15
    pages.append("\n".join(operations))
    objects: list[bytes] = [
        b"<< /Type /Catalog /Pages 2 0 R >>", b"",
        b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>",
    ]
    kids = []
    for content in pages:
        page_id = len(objects) + 1
        stream_id = page_id + 1
        kids.append(f"{page_id} 0 R")
        objects.append((f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] "
                        f"/Resources << /Font << /F1 3 0 R /F2 4 0 R >> >> "
                        f"/Contents {stream_id} 0 R >>").encode("ascii"))
        raw = content.encode("ascii")
        objects.append(f"<< /Length {len(raw)} >>\nstream\n".encode() + raw + b"\nendstream")
    objects[1] = f"<< /Type /Pages /Count {len(kids)} /Kids [{' '.join(kids)}] >>".encode()
    output = bytearray(b"%PDF-1.4\n")
    offsets = [0]
    for number, body in enumerate(objects, 1):
        offsets.append(len(output))
        output.extend(f"{number} 0 obj\n".encode() + body + b"\nendobj\n")
    xref = len(output)
    output.extend(f"xref\n0 {len(offsets)}\n0000000000 65535 f \n".encode())
    for offset in offsets[1:]:
        output.extend(f"{offset:010d} 00000 n \n".encode())
    output.extend(f"trailer\n<< /Size {len(offsets)} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n".encode())
    return bytes(output)


def build_efi(files: dict[str, bytes], provenance: dict) -> tuple[dict[str, bytes], dict]:
    if os.name == "nt":
        raise ValueError("--build requires Linux/WSL and pinned LLVM/LLD")
    toolchain = ROOT / ".toolchain"
    if not (toolchain / "llvm").is_dir():
        raise ValueError("Missing .toolchain/llvm; follow the pinned bootstrap instructions")
    products: list[dict[str, bytes]] = []
    logs = {}
    environment = {**os.environ, "SOURCE_DATE_EPOCH": str(provenance["source_date_epoch"]),
                   "TZ": "UTC", "LC_ALL": "C"}
    for iteration in (1, 2):
        with tempfile.TemporaryDirectory(prefix="rros-community-build-") as directory:
            work = Path(directory)
            for name, data in files.items():
                target = work / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
            (work / ".toolchain").symlink_to(toolchain.resolve(), target_is_directory=True)
            built = {}
            for architecture, executable in (("x86_64", "BOOTX64.EFI"), ("aarch64", "BOOTAA64.EFI")):
                result = subprocess.run(["sh", f"scripts/build-{architecture}.sh"],
                                        cwd=work, env=environment, capture_output=True, text=True)
                label = f"{architecture}-{iteration}"
                logs[label] = (result.stdout + result.stderr).replace(
                    str(work), "<source>").replace(str(toolchain.resolve()), "<toolchain>")
                if result.returncode:
                    raise RuntimeError(f"EFI build failed ({label}):\n{logs[label]}")
                path = work / "build" / "dev" / architecture / "esp" / "EFI" / "BOOT" / executable
                data = path.read_bytes()
                if not data.startswith(b"MZ"):
                    raise ValueError(f"Invalid PE/COFF output: {architecture}")
                built[f"{architecture}/{executable}"] = data
            products.append(built)
    if products[0] != products[1]:
        raise ValueError("EFI outputs differ between isolated builds")
    return products[0], {"status": "PASS", "scope": "two isolated software builds; no physical execution",
                         "outputs": {n: digest(d) for n, d in products[0].items()},
                         "compiler_logs": logs}


def verify_output(directory: Path) -> None:
    manifest = json.loads((directory / "release-manifest.json").read_bytes())
    declared = set()
    for line in (directory / "SHA256SUMS").read_text(encoding="ascii").splitlines():
        checksum, name = line.split("  ", 1)
        safe_name(name)
        if name in declared:
            raise ValueError(f"Duplicate download checksum: {name}")
        declared.add(name)
        if digest((directory / name).read_bytes()) != checksum:
            raise ValueError(f"Download checksum mismatch: {name}")
    expected = manifest["artifacts"]
    actual = {p.name for p in directory.iterdir() if p.is_file()}
    if actual != set(expected) | {"release-manifest.json", "SHA256SUMS"}:
        raise ValueError("Unexpected/missing download files")
    if declared != actual - {"SHA256SUMS"}:
        raise ValueError("Incomplete download checksum coverage")
    for name, checksum in expected.items():
        data = (directory / name).read_bytes()
        if digest(data) != checksum:
            raise ValueError(f"Manifest checksum mismatch: {name}")
        if name.endswith(".zip"):
            verify_zip(data)
    if manifest["hardware_verification"] != "NOT_VERIFIED":
        raise ValueError("Invalid physical verification claim")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--commit", default="HEAD")
    parser.add_argument("--source-tree", action="store_true")
    parser.add_argument("--build", action="store_true")
    parser.add_argument("--output", type=Path, default=ROOT / "build" / "community" / VERSION)
    parser.add_argument("--verify", type=Path)
    args = parser.parse_args()
    try:
        if args.verify:
            verify_output(args.verify)
            print(f"PASS download and archive integrity: {args.verify}")
            return 0
        if args.output.exists():
            raise ValueError(f"Refusing existing output: {args.output}")
        files, provenance = source(args.commit, args.source_tree)
        required = ("LICENSE", "docs/community/SAFETY_GATE.json", "docs/community/HARDWARE_GUIDE.md",
                    "tests/pi5_hw_validation/tester.py", "scripts/pi5_hw/community_tester.py")
        for name in required:
            if name not in files:
                raise ValueError(f"Required committed source is missing: {name}")
        owned = json.loads(files["docs/license-owned-files.json"])
        owned["files"] = [name for name in owned["files"] if name in files]
        owned["distribution_scope"] = "Filtered community source archive; exclusions in SOURCE_PROVENANCE.json"
        files["docs/license-owned-files.json"] = json_bytes(owned)
        gate = json.loads(files["docs/community/SAFETY_GATE.json"])
        if gate["physical_operations_approved"] or gate["hardware_verification"] != "NOT_VERIFIED":
            raise ValueError("This release requires a fail-closed physical safety gate")
        provenance["version"] = VERSION
        files["SOURCE_PROVENANCE.json"] = json_bytes(provenance)
        outputs = {f"{PREFIX}-source.zip": zip_bytes(files)}
        sdk = {n: d for n, d in files.items() if n.startswith("sdk/")
               or n in {"LICENSE", "THIRD_PARTY_NOTICES.md", "kernel/abi.h",
                        "tools/eospkg.py", "tools/mkimage.py", "tools/checkimage.py",
                        "SOURCE_PROVENANCE.json"}}
        sdk["THIRD_PARTY_NOTICES.md"] = standalone_notices(
            files["THIRD_PARTY_NOTICES.md"], provenance["source_commit"])
        sdk["README.md"] = (
            "# RROS v1 SDK sources\n\nHeaders/examples are under sdk; kernel/abi.h supplies "
            "the ABI definition. Original packaging/image tools are under tools.\n"
            "This is not a native application-loader executable or a physical Pi deployment kit.\n"
            "Use the matching source ZIP and docs/community/RELEASE.md for verified builds/tests.\n"
            "Exact source revision and exclusions: SOURCE_PROVENANCE.json. "
            "Original assessed code: GPL-3.0-or-later; third-party terms remain separate.\n").encode()
        outputs[f"{PREFIX}-SDK.zip"] = zip_bytes(sdk)
        pdf = pdf_guide(files["docs/community/HARDWARE_GUIDE.md"].decode("utf-8"),
                        provenance["source_commit"],
                        files["docs/community/BOM.csv"].decode("utf-8"))
        outputs[f"{PREFIX}-hardware-guide.pdf"] = pdf
        kit = {Path(n).name: files[n] for n in
               ("LICENSE", "THIRD_PARTY_NOTICES.md", "docs/community/BOM.csv",
                "docs/community/HARDWARE_GUIDE.md", "docs/community/SAFETY_GATE.json",
                "docs/community/SAFETY_REVIEW.md", "docs/community/EVIDENCE.md",
                "docs/community/ELECTRICAL_PRE_POWER.md",
                "scripts/pi5_hw/community_tester.py",
                "tests/pi5_hw_validation/tester.py",
                "tests/pi5_hw_validation/evaluate_probe_log.py", "SOURCE_PROVENANCE.json")}
        kit["hardware-guide.pdf"] = pdf
        kit["scripts/pi5_hw/boot_chain_pins.json"] = files["scripts/pi5_hw/boot_chain_pins.json"]
        kit["THIRD_PARTY_NOTICES.md"] = standalone_notices(
            files["THIRD_PARTY_NOTICES.md"], provenance["source_commit"])
        kit["README.md"] = (
            "# RROS Pi 5 community engineering-review kit\n\n"
            "Start with hardware-guide.pdf, HARDWARE_GUIDE.md, BOM.csv and SAFETY_GATE.json.\n"
            "No physical operation is approved. Python 3.11+ is required for offline tools.\n"
            "Run: python community_tester.py demo --out rehearsal\n"
            "Review EVIDENCE.md and redact every archive before voluntary submission.\n"
            "No firmware, serial driver, flash tool, or physical boot image is included.\n").encode()
        outputs[f"{PREFIX}-Pi5-review.zip"] = zip_bytes(kit)
        build = {"status": "NOT_VERIFIED", "reason": "--build not selected"}
        if args.build:
            binaries, build = build_efi(files, provenance)
            binaries.update({n: files[n] for n in
                             ("LICENSE", "THIRD_PARTY_NOTICES.md", "SOURCE_PROVENANCE.json")})
            binaries["THIRD_PARTY_NOTICES.md"] = standalone_notices(
                files["THIRD_PARTY_NOTICES.md"], provenance["source_commit"])
            binaries["README.md"] = (
                "Software-only EFI development outputs. NOT a physical Raspberry Pi 5 image.\n"
                "No firmware is bundled. Physical boot and peripheral tests are BLOCKED.\n").encode()
            outputs[f"{PREFIX}-software.zip"] = zip_bytes(binaries)
        outputs["RELEASE_NOTES.md"] = files["docs/community/RELEASE_NOTES.md"].replace(
            b"@SOURCE_COMMIT@", provenance["source_commit"].encode("ascii"))
        outputs["KNOWN_LIMITATIONS.md"] = files["docs/KNOWN_LIMITATIONS.md"]
        manifest = {**provenance, "artifacts": {n: digest(d) for n, d in sorted(outputs.items())},
                    "build": build, "packager_python": platform.python_version(),
                    "published": False}
        outputs["release-manifest.json"] = json_bytes(manifest)
        outputs["SHA256SUMS"] = "".join(
            f"{digest(d)}  {n}\n" for n, d in sorted(outputs.items())).encode("ascii")
        args.output.mkdir(parents=True)
        for name, data in outputs.items():
            (args.output / name).write_bytes(data)
        verify_output(args.output)
        print(f"PASS {VERSION}: {len(outputs)} verified files at {args.output}")
        return 0
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"FAIL community release: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
