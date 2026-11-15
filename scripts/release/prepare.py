#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RROS contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build and test a public candidate in a private local clone; never publish."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
VERSION = "0.1.0-experimental.1"
SOURCE_DIRS = ("arch", "boot", "kernel", "sdk", "scripts", "tests", "tools", "docs", ".github")
SOURCE_FILES = ("README.md", "README.da.md", "MASTER_PROMPT.md",
                ".gitattributes", ".gitignore", "HARDWARE_VALIDATION.md",
                "RASPBERRY_PI5_TEST_GUIDE.md", "RASPBERRY_PI5_TEST_GUIDE.da.md",
                "HARDWARE_COMPATIBILITY_MATRIX.md", "COMMUNITY_TEST_REPORT_TEMPLATE.md",
                "CONTRIBUTING.md", "SECURITY.md", "THIRD_PARTY_NOTICES.md", "LICENSE")
PAYLOADS = ("build/x86_64/ExperimentalOS.iso", "build/aarch64/ExperimentalOS-Pi5.img",
            "build/x86_64/esp/EFI/BOOT/BOOTX64.EFI", "build/aarch64/esp/EFI/BOOT/BOOTAA64.EFI",
            "build/sdk/ExperimentalOS-SDK-0.1.0-rc1.tar.gz",
            "build/sdk/RPi5-DevDeploy-Manager-Resources-0.1.0-rc1.tar.gz")


def sha(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def source_paths(root: Path) -> list[Path]:
    paths = [root / name for name in SOURCE_FILES if (root / name).is_file()]
    for name in SOURCE_DIRS:
        paths.extend(p for p in (root / name).rglob("*")
                     if p.is_file() and "__pycache__" not in p.parts
                     and not p.name.endswith((".pyc", ".pyo"))
                     and not p.relative_to(root).as_posix().startswith(".github/agents/"))
    return sorted(paths)


def source_digest(root: Path) -> str:
    digest = hashlib.sha256()
    for path in source_paths(root):
        digest.update(path.relative_to(root).as_posix().encode() + b"\0")
        digest.update(bytes.fromhex(sha(path)))
    return digest.hexdigest()


def archive(target: Path, files: dict[str, Path]) -> None:
    with target.open("wb") as raw, gzip.GzipFile(fileobj=raw, mode="wb", filename="", mtime=0) as gz:
        with tarfile.open(fileobj=gz, mode="w", format=tarfile.PAX_FORMAT) as tar:
            for name, path in sorted(files.items()):
                if path.is_symlink():
                    raise ValueError(f"Refusing archive symlink: {path}")
                info = tarfile.TarInfo(name)
                info.size = path.stat().st_size
                info.mtime = 1767225600
                info.mode = 0o755 if path.suffix == ".sh" else 0o644
                with path.open("rb") as stream:
                    tar.addfile(info, stream)


def run(work: Path, evidence: Path, name: str, command: list[str],
        results: list[dict[str, object]], env: dict[str, str] | None = None) -> str:
    log = evidence / f"{name}.log"
    print(f"RUN {name}: {' '.join(command)}", flush=True)
    with log.open("w", encoding="utf-8") as output:
        completed = subprocess.run(command, cwd=work, stdout=output, stderr=subprocess.STDOUT,
                                   env={**os.environ, **(env or {})}, check=False)
    text = log.read_text(encoding="utf-8", errors="replace")
    results.append({"name": name, "command": command, "exit_code": completed.returncode,
                    "status": "PASS" if completed.returncode == 0 else "FAIL", "log": log.name,
                    "log_sha256": sha(log)})
    print(f"{results[-1]['status']} {name}", flush=True)
    if completed.returncode:
        print(text[-12000:], file=sys.stderr)
        raise RuntimeError(f"{name} failed; preserved log: {log}")
    return text


def package(work: Path, out: Path, evidence: Path) -> list[Path]:
    products: list[Path] = []
    for label, mapping in (
        ("x86_64", {"ExperimentalOS.iso": work / PAYLOADS[0],
                    "EFI/BOOT/BOOTX64.EFI": work / PAYLOADS[2]}),
        ("aarch64-qemu", {"ExperimentalOS-Pi5.img": work / PAYLOADS[1],
                          "EFI/BOOT/BOOTAA64.EFI": work / PAYLOADS[3]}),
    ):
        path = out / f"RROS-{VERSION}-{label}.tar.gz"
        mapping["README.md"] = work / "docs/release/RELEASE_NOTES.md"
        mapping["LICENSE"] = work / "LICENSE"
        archive(path, mapping)
        products.append(path)
    for label, relative in (("SDK", PAYLOADS[4]), ("DevDeploy-resources", PAYLOADS[5])):
        path = out / f"RROS-{VERSION}-{label}.tar.gz"
        shutil.copyfile(work / relative, path)
        products.append(path)
    probe = work / "build/pi5-probe-package"
    excluded = {"card/RPI_EFI.fd", "card/config.txt", "card/bcm2712-rpi-5-b.dtb",
                "PACKAGE_SHA256SUMS"}
    mapping = {p.relative_to(probe).as_posix(): p for p in probe.rglob("*")
               if p.is_file() and p.relative_to(probe).as_posix() not in excluded}
    mapping.update({name: work / name for name in (
        "RASPBERRY_PI5_TEST_GUIDE.md", "RASPBERRY_PI5_TEST_GUIDE.da.md",
        "HARDWARE_COMPATIBILITY_MATRIX.md", "THIRD_PARTY_NOTICES.md",
        "LICENSE",
        "tests/pi5_hw_validation/selftest.py",
        "tests/pi5_hw_validation/evaluate_serial_log.py",
        "scripts/pi5_hw/fdt_summary.py", "tools/checkimage.py")})
    # Firmware remains an independently downloaded, hash-pinned dependency.
    sums = evidence / "probe-PACKAGE_SHA256SUMS"
    sums.write_text("".join(f"{sha(p)}  {name}\n" for name, p in sorted(mapping.items())), encoding="utf-8")
    mapping["PACKAGE_SHA256SUMS"] = sums
    path = out / f"RROS-{VERSION}-Pi5-probe.tar.gz"
    archive(path, mapping)
    products.append(path)
    mapping = {p.relative_to(work).as_posix(): p for p in source_paths(work) if p.name != "OSBuild.pdf"}
    path = out / f"RROS-{VERSION}-source.tar.gz"
    archive(path, mapping)
    products.append(path)
    mapping = {p.relative_to(evidence).as_posix(): p for p in evidence.rglob("*") if p.is_file()}
    mapping.update({p.relative_to(work).as_posix(): p for p in (work / "logs").rglob("*")
                    if p.is_file() and "history" not in p.parts})
    path = out / f"RROS-{VERSION}-evidence.tar.gz"
    archive(path, mapping)
    products.append(path)
    return products


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=ROOT / "build/public-release" / VERSION)
    parser.add_argument("--tooling-only", action="store_true")
    args = parser.parse_args()
    if os.name == "nt":
        parser.error("Run under WSL/Linux; see docs/release/PUBLISHING.md")
    out = args.output.resolve()
    if out.exists():
        parser.error(f"Refusing to overwrite candidate directory: {out}")
    out.mkdir(parents=True)
    evidence = out / "verification"
    evidence.mkdir()
    results: list[dict[str, object]] = []
    head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True,
                          capture_output=True, check=True).stdout.strip()
    report: dict[str, object] = {
        "schema_version": 1, "name": "RoboticRasOS (RROS)", "version": VERSION,
        "source_commit": head, "source_snapshot_sha256": source_digest(ROOT),
        "source_tree_dirty": bool(subprocess.run(
            ["git", "status", "--porcelain", "--", *SOURCE_DIRS, *SOURCE_FILES,
             ":(exclude).github/agents"], cwd=ROOT,
            text=True, capture_output=True, check=True).stdout),
        "started_at": datetime.now(timezone.utc).isoformat(), "results": results,
        "hardware_status": "HARDWARE_NOT_VERIFIED", "published": False,
        "github_actions_status": "EXECUTED" if os.environ.get("GITHUB_ACTIONS") == "true" else "NOT_EXECUTED",
        "publication_blockers": [
            "Public distribution is not authorized; private first-stage repository is Janus5G/RROS.",
            "Committed/tagged source and software verification required for the private release.",
            "Owned source is GPL-3.0-or-later; third-party fixture notices/data rights remain unconfirmed.",
            "Firmware redistribution terms (EDK2, TF-A, edk2-non-osi/Broadcom) need owner review.",
            "Historical reference PDF redistribution rights unresolved; absent from current source and excluded from archives.",
        ],
    }
    try:
        with tempfile.TemporaryDirectory(prefix="rros-release-") as temporary:
            work = Path(temporary) / "repo"
            subprocess.run(["git", "clone", "--quiet", "--local", "--no-hardlinks", str(ROOT), str(work)], check=True)
            subprocess.run(["git", "checkout", "--quiet", "--detach", head], cwd=work, check=True)
            # Do not inherit old evidence or release outputs from the tracked checkout.
            for name in ("build", "logs"):
                shutil.rmtree(work / name)
            (work / "build").mkdir()
            shutil.copyfile(ROOT / "build/gate_state.json", work / "build/gate_state.json")
            for path in source_paths(ROOT):
                target = work / path.relative_to(ROOT)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path, target)
            subprocess.run(["git", "add", "--", *SOURCE_DIRS,
                            *[n for n in SOURCE_FILES if (work / n).exists()]], cwd=work, check=True)
            # Existing provisioned tools are read-only shared inputs, not cloned.
            (work / ".toolchain").symlink_to(ROOT / ".toolchain", target_is_directory=True)
            external = ROOT / "build/external"
            if external.exists():
                (work / "build/external").symlink_to(external, target_is_directory=True)
            pins = json.loads((work / "scripts/pi5_hw/boot_chain_pins.json").read_text())
            cache = work / "build/pi5-boot-chain-cache"
            cache.mkdir()
            firmware = ROOT / "build/pi5-boot-chain-cache" / pins["zip"]["name"]
            if not firmware.is_file():
                raise FileNotFoundError(f"Missing pinned firmware; fetch as documented: {pins['url']}")
            shutil.copyfile(firmware, cache / firmware.name)
            if sha(cache / firmware.name) != pins["zip"]["sha256"]:
                raise ValueError("Pinned firmware SHA-256 mismatch")
            with zipfile.ZipFile(cache / firmware.name) as z:
                dtb = z.read("bcm2712-rpi-5-b.dtb")
            if hashlib.sha256(dtb).hexdigest() != pins["files"]["bcm2712-rpi-5-b.dtb"]["sha256"]:
                raise ValueError("Pinned DTB SHA-256 mismatch")
            (cache / "bcm2712-rpi-5-b.dtb").write_bytes(dtb)
            run(work, evidence, "gate-runner-selftest", [sys.executable, "tests/unit/test_gate_runner.py"], results)
            run(work, evidence, "release-tooling-selftest", [sys.executable, "tests/unit/test_release.py"], results)
            run(work, evidence, "CR01-CR07-regressions",
                [sys.executable, "tests/pi5_hw_validation/test_review_regressions.py",
                 "--report", str(evidence / "code-review.json")], results)
            run(work, evidence, "hardware-tooling-selftest", [sys.executable, "tests/pi5_hw_validation/selftest.py"], results)
            run(work, evidence, "probe-console-host", ["sh", "scripts/test-boot-console-host.sh",
                                                     str(cache / "bcm2712-rpi-5-b.dtb")], results)
            if not args.tooling_only:
                for number in range(21):
                    gate = f"G{number:02d}"
                    run(work, evidence, gate, [sys.executable, "scripts/gates/run_gate.py", gate], results)
                    # The gate runner prints the actual report output, including optional-check status.
                    gate_report = json.loads((work / f"logs/{gate}_RESULT.json").read_text())
                    actual = gate_report["actual_results"]
                    if gate == "G12":
                        shutil.copytree(work / "build/dev/aarch64/esp", work / "build/public-driver-esp")
                    if gate == "G15" and "EXECUTED_PASS" not in actual:
                        results.append({"name": "G15-live-upstream", "status": "NOT_TESTABLE",
                                        "reason": "Pinned RoboForge checkout/Node absent; fixture tests only."})
                    if gate == "G16" and "EXECUTED_PASS" not in actual:
                        results.append({"name": "G16-upstream-crosscheck", "status": "NOT_TESTABLE",
                                        "reason": "Pinned ChromaNeural checkout absent; committed vectors tested."})
                run(work, evidence, "G21", [sys.executable, "scripts/gates/run_gate.py", "G21",
                                           "--test-arg=--reuse-regressions"], results)
                hashes = []
                for iteration in (1, 2):
                    run(work, evidence, f"release-build-{iteration}", ["sh", "scripts/build-release.sh"], results,
                        {"EXPERIMENTALOS_ALLOW_RELEASE_REBUILD": "1"})
                    hashes.append({p: sha(work / p) for p in PAYLOADS})
                if hashes[0] != hashes[1]:
                    raise ValueError("Release payloads differ between two isolated clean builds")
                results.append({"name": "release-reproducibility", "status": "PASS", "sha256": hashes[1]})
                run(work, evidence, "G22", [sys.executable, "scripts/gates/run_gate.py", "G22",
                                           "--test-arg=--check-existing"], results)
                run(work, evidence, "uefi-harness", [sys.executable, "tests/pi5_uefi_harness/harness.py",
                                                   "--timeout", "90", "--driver-esp", "build/public-driver-esp"], results)
                run(work, evidence, "hardware-tooling-with-real-qemu-log",
                    [sys.executable, "tests/pi5_hw_validation/selftest.py"], results)
                run(work, evidence, "probe-build", ["sh", "scripts/build-pi5-probe.sh"], results)
                run(work, evidence, "probe-qemu", ["sh", "scripts/run-pi5-probe-qemu.sh"], results,
                    {"EXPERIMENTALOS_QEMU_TIMEOUT": "90"})
                run(work, evidence, "probe-package", [sys.executable, "scripts/pi5_hw/build_probe_package.py"], results)
                results.append({"name": "physical-Pi5-subsystems", "status": "NOT_TESTABLE",
                                "reason": "No physical board on hosted runners; see hardware compatibility matrix."})
                products = package(work, out, evidence)
                report["artifacts"] = [{"name": p.name, "sha256": sha(p), "size": p.stat().st_size} for p in products]
            report["software_status"] = "PASS" if not args.tooling_only else "NOT_TESTABLE"
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as exc:
        report["software_status"] = "FAIL"
        report["error"] = str(exc)
        print(f"FAIL: {exc}", file=sys.stderr)
    report["finished_at"] = datetime.now(timezone.utc).isoformat()
    manifest = out / "release-manifest.json"
    manifest.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    files = sorted(p for p in out.iterdir() if p.is_file() and p.name != "SHA256SUMS")
    (out / "SHA256SUMS").write_text("".join(f"{sha(p)}  {p.name}\n" for p in files), encoding="utf-8")
    print(f"Candidate: {out}\nSoftware: {report['software_status']}\nHardware: HARDWARE_NOT_VERIFIED")
    return 1 if report["software_status"] == "FAIL" else 0


if __name__ == "__main__":
    raise SystemExit(main())
