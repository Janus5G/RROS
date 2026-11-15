#!/usr/bin/env python3
"""Validate G22: local experimental release candidate (images, SDK, manifests, docs). No publishing."""

from __future__ import annotations

import hashlib
import argparse
import json
import os
import re
import shlex
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import checkimage  # noqa: E402

ISO = ROOT / "build" / "x86_64" / "ExperimentalOS.iso"
IMG = ROOT / "build" / "aarch64" / "ExperimentalOS-Pi5.img"
EFI_X = ROOT / "build" / "x86_64" / "esp" / "EFI" / "BOOT" / "BOOTX64.EFI"
EFI_A = ROOT / "build" / "aarch64" / "esp" / "EFI" / "BOOT" / "BOOTAA64.EFI"
SDK = ROOT / "build" / "sdk"
MAN = ROOT / "build" / "manifests"
REPRO = [ISO, IMG, EFI_X, EFI_A, SDK / "ExperimentalOS-SDK-0.1.0-rc1.tar.gz",
         SDK / "RPi5-DevDeploy-Manager-Resources-0.1.0-rc1.tar.gz"]


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


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check-existing", action="store_true",
                        help="read-only artifact checks; caller must separately verify reproducibility")
    args = parser.parse_args()
    if not args.check_existing:
        run_command(["sh", "scripts/build-release.sh"])
        first = {p.name: sha(p) for p in REPRO}
        run_command(["sh", "scripts/build-release.sh"])
        for p in REPRO:
            check(sha(p) == first[p.name], f"G22-REPRODUCIBLE-{p.name}", f"{p.name} is bit-identical across two clean release builds")
    else:
        print("G22 build/reproducibility stage NOT RUN here: validated separately by the release orchestrator.")

    info = checkimage.check_disk(IMG, "EFI/BOOT/BOOTAA64.EFI", "aarch64", EFI_A)
    check(info["fat_bits"] == 32, "G22-PI5-IMAGE-STRUCTURE", "GPT+CRC, ESP type, FAT32, boot app read back and PE machine AArch64")
    check(IMG.stat().st_size % 512 == 0, "G22-PI5-IMAGE-SECTOR-ALIGNED", "image size is sector aligned")

    # Negative checks: structural checker must reject corruption.
    data = bytearray(IMG.read_bytes())
    for label, offset in (("PRIMARY-GPT-HEADER", 512 + 20), ("BOOT-APP", bytes(data).find(EFI_A.read_bytes()[:4096]) + 100)):
        bad = bytearray(data)
        bad[offset] ^= 0xFF
        tmp = ROOT / "build" / "g22-bad.img"
        tmp.write_bytes(bad)
        try:
            refused = False
            try:
                checkimage.check_disk(tmp, "EFI/BOOT/BOOTAA64.EFI", "aarch64", EFI_A)
            except checkimage.CheckError:
                refused = True
            check(refused, f"G22-CHECKER-REFUSES-CORRUPT-{label}", "corrupted image is rejected")
        finally:
            tmp.unlink(missing_ok=True)
    wrong = False
    try:
        checkimage.check_disk(IMG, "EFI/BOOT/BOOTAA64.EFI", "x86_64", EFI_A)
    except checkimage.CheckError:
        wrong = True
    check(wrong, "G22-CHECKER-REFUSES-WRONG-ARCHITECTURE", "architecture mismatch is rejected")

    report = run_command(["xorriso", "-indev", "build/x86_64/ExperimentalOS.iso", "-report_el_torito", "plain"])
    check("El Torito" in report and "efi" in report.lower(), "G22-ISO-EL-TORITO-EFI", "ISO declares an EFI El Torito boot image")
    esp = run_command(["xorriso", "-indev", "build/x86_64/ExperimentalOS.iso", "-find", "/", "-name", "BOOTX64.EFI"])
    check("BOOTX64.EFI" in esp, "G22-ISO-CONTAINS-BOOT-APP", "ISO tree contains EFI/BOOT/BOOTX64.EFI")
    iso_bytes = ISO.read_bytes()
    check(EFI_X.read_bytes() in iso_bytes, "G22-ISO-PAYLOAD-MATCHES-BUILD", "ISO embeds the exact build artifact")
    checkimage.check_pe(EFI_X.read_bytes(), "x86_64")

    boot = run_command(["sh", "scripts/test-release-boot.sh"])
    print(boot)
    check("PASS RELEASE-ISO-BOOTS-IN-OVMF-GUEST" in boot, "G22-ISO-BOOTS-IN-QEMU", "ISO boots in an OVMF guest")
    check("PASS RELEASE-PI5-IMAGE-BOOTS-AS-GENERIC-AARCH64-UEFI-GUEST" in boot, "G22-PI5-IMAGE-BOOTS-IN-QEMU-GENERIC",
          "Pi image boots as generic AArch64 UEFI guest (not Pi hardware)")

    release = json.loads((MAN / "release-manifest.json").read_text())
    check(release["hardware_status"] == "HARDWARE_NOT_VERIFIED" and release["published"] is False,
          "G22-MANIFEST-HONEST-STATUS", "manifest states hardware not verified and unpublished")
    check("not boot-verified" in release["raspberry_pi_5"], "G22-MANIFEST-PI5-NOT-BOOT-VERIFIED", "manifest does not claim Pi boot")
    check(re.fullmatch(r"[0-9a-f]{40}", release["source_commit"]) is not None, "G22-MANIFEST-SOURCE-COMMIT", "manifest records source commit")
    for entry in release["artifacts"]:
        check(sha(ROOT / entry["path"]) == entry["sha256"] and not entry["executable_json"],
              f"G22-MANIFEST-HASH-{entry['id']}", "artifact hash matches manifest")
    for key in ("build/x86_64/kernel-debug.elf", "build/aarch64/kernel-debug.elf", "development-deployment-manager-executable"):
        check(key in release["absent_artifacts"] and not (ROOT / key).exists(), f"G22-ABSENT-RECORDED-{key}",
              "missing artifact is reported absent, not faked")
    sums = (MAN / "SHA256SUMS").read_text().splitlines()
    for line in sums:
        digest, _, rel = line.partition("  ")
        check(sha(ROOT / rel) == digest, f"G22-SHA256SUMS-{rel}", "listed file matches SHA-256")
    for text in list(MAN.glob("*.json")):
        check(not re.search(r"PRIVATE KEY|private_key\"\s*:\s*\"[^\"]", text.read_text()), f"G22-NO-PRIVATE-KEY-{text.name}",
              "manifest exports no private key material")

    listing = run_command(["tar", "-tzf", "build/sdk/ExperimentalOS-SDK-0.1.0-rc1.tar.gz"])
    for member in ("include/experimentalos.h", "kernel/abi.h", "tools/eospkg.py", "docs/SECURITY.md", "examples/hello.c"):
        check(member in listing, f"G22-SDK-CONTAINS-{member}", "SDK package contains member")
    ddm = run_command(["tar", "-tzf", "build/sdk/RPi5-DevDeploy-Manager-Resources-0.1.0-rc1.tar.gz"])
    check("README.txt" in ddm and "kernel/mcp_deploy.c" in ddm, "G22-DDM-RESOURCES-PACKAGED-SEPARATELY", "manager resources packaged separately")
    check(not re.search(r"\.(pem|key)$|\.exe$", listing + ddm, re.M), "G22-NO-KEYS-OR-FAKE-EXECUTABLES", "no key files or executables in packages")

    for name in ("SECURITY.md", "HARDWARE_VALIDATION_GUIDE.md", "REPRODUCIBLE_BUILD.md", "RELEASE_NOTES.md", "KNOWN_LIMITATIONS.md", "G22_PROGRESS.md"):
        check((ROOT / "docs" / name).is_file(), f"G22-DOC-{name}", "required document exists")
    progress = (ROOT / "docs" / "G22_PROGRESS.md").read_text(encoding="utf-8")
    security = (ROOT / "docs" / "SECURITY.md").read_text(encoding="utf-8")
    for needle in ("Not implemented", "HARDWARE_NOT_VERIFIED", "compatibility-pending", "not boot-verified"):
        check(needle in progress, f"G22-DOC-{needle.upper().replace(' ', '-')}", f"progress record states '{needle}'")
    for needle in ("symmetric", "entropy", "enrollment", "rollback"):
        check(needle in security, f"G22-SECURITY-{needle.upper()}", f"security doc covers '{needle}'")
    print("G22 release candidate checks complete; hardware remains HARDWARE_NOT_VERIFIED; nothing was published.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL G22: {exc}", file=sys.stderr)
        raise SystemExit(1)
