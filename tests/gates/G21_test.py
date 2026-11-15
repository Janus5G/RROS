#!/usr/bin/env python3
"""Validate G21 system-wide integration: integration, dependency audit, reproducibility, full regression."""

from __future__ import annotations

import hashlib
import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
KEY_IDS = [
    "G21-DOWNGRADE-FLOOR-SURVIVES-RESTART",
]
ALLOWED_KERNEL_INCLUDES = {"stddef.h", "stdint.h"}
ARTIFACTS = {
    "x86_64": ROOT / "build" / "dev" / "x86_64" / "esp" / "EFI" / "BOOT" / "BOOTX64.EFI",
    "aarch64": ROOT / "build" / "dev" / "aarch64" / "esp" / "EFI" / "BOOT" / "BOOTAA64.EFI",
}


def check(condition: bool, test_id: str, message: str) -> None:
    if not condition:
        raise AssertionError(f"FAIL {test_id}: {message}")
    print(f"PASS {test_id}: {message}")


def run_command(command: list[str], allow_fail: bool = False) -> str:
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
    if result.returncode and not allow_fail:
        raise RuntimeError(f"Command failed ({result.returncode}):\n{result.stdout}")
    return result.stdout.strip()


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def clean_build() -> dict[str, str]:
    hashes = {}
    for arch in ("x86_64", "aarch64"):
        shutil.rmtree(ROOT / "build" / "dev" / arch, ignore_errors=True)
        run_command(["sh", f"scripts/build-{arch}.sh"])
        check(ARTIFACTS[arch].is_file(), f"G21-CLEAN-BUILD-{arch.upper()}", f"clean {arch} build produced its EFI application")
        hashes[arch] = sha256(ARTIFACTS[arch])
    return hashes


def dependency_audit() -> None:
    host_only = {"kernel/boot_console_test.c": "scripts/test-boot-console-host.sh"}
    production_builds = [
        (ROOT / "scripts" / name).read_text(encoding="utf-8")
        for name in ("build-x86_64.sh", "build-aarch64.sh", "build-pi5-probe.sh")
    ]
    for sub in ("kernel", "arch", "boot"):
        for path in sorted((ROOT / sub).rglob("*")):
            if path.suffix not in (".c", ".h", ".S", ".s"):
                continue
            relative = path.relative_to(ROOT).as_posix()
            if relative in host_only:
                host_script = (ROOT / host_only[relative]).read_text(encoding="utf-8")
                check(relative in host_script and all(path.stem not in build for build in production_builds),
                      f"G21-AUDIT-HOST-ONLY-{path.name}",
                      "standalone host test is compiled only by its host runner, not a firmware build")
                continue
            guarded = False
            for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
                stripped = line.strip()
                if stripped.startswith("#if"):
                    guarded = "TEST_DIAGNOSTICS" in stripped or guarded
                elif stripped.startswith("#endif"):
                    guarded = False
                match = re.match(r"#\s*include\s*<([^>]+)>", stripped)
                if match:
                    name = match.group(1)
                    ok = name in ALLOWED_KERNEL_INCLUDES or (name == "stdio.h" and guarded)
                    check(ok, f"G21-AUDIT-INCLUDE-{path.name}-{name}",
                          f"{path.relative_to(ROOT).as_posix()} includes <{name}> only freestanding or test-guarded")
    check(not (ROOT / ".gitmodules").exists(), "G21-AUDIT-NO-SUBMODULES", "no git submodules or vendored upstream trees")
    net = re.compile(r"\b(curl|wget|git\s+clone|pip\s+install|npm\s+install|Invoke-WebRequest)\b")
    for path in sorted((ROOT / "scripts").glob("build-*.sh")):
        check(not net.search(path.read_text(encoding="utf-8")), f"G21-AUDIT-NO-FETCH-{path.name}",
              f"{path.name} performs no network fetches")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--reuse-regressions", action="store_true",
                        help="verify source-bound G00-G20 reports instead of running them twice")
    args = parser.parse_args()
    output = run_command(["sh", "scripts/test-integration-host.sh"])
    print(output)
    check("FAIL G21" not in output, "G21-NO-FAILURES", "integration host tests reported no failures")
    check(output.count("G21 INTEGRATION RESULT: PASS") == 2, "G21-INTEGRATION-HOST",
          "integration tests pass in plain and ASan/UBSan builds")
    source = (ROOT / "tests" / "gates" / "G21_integration_test.c").read_text(encoding="utf-8")
    ids = sorted(set(re.findall(r'"(G21-[A-Z0-9-]+)"\)', source)))
    check(len(ids) >= 20, "G21-CHECK-COUNT", f"{len(ids)} distinct integration checks defined")
    for test_id in ids:
        check(len(re.findall(rf"^PASS {re.escape(test_id)}$", output, re.M)) == 2, test_id,
              "check present and passing in both builds")
    for test_id in KEY_IDS:
        check(test_id in ids, f"KEY-{test_id}", "key safety check is defined")

    dependency_audit()

    first = clean_build()
    second = clean_build()
    for arch in first:
        check(first[arch] == second[arch], f"G21-REPRODUCIBLE-{arch.upper()}",
              f"two clean {arch} builds are bit-identical ({first[arch][:16]})")

    for number in range(0, 21):
        script = ROOT / "tests" / "gates" / f"G{number:02d}_test.py"
        if not script.is_file():
            continue
        if args.reuse_regressions:
            report = json.loads((ROOT / "logs" / f"G{number:02d}_RESULT.json").read_text(encoding="utf-8"))
            head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True,
                                  capture_output=True, check=True).stdout.strip()
            check(report["source_commit"] == head and report["software_status"] == "SOFTWARE_PASS"
                  and report["exit_codes"] == [0], f"G21-REGRESSION-G{number:02d}",
                  "previous stage has a passing report at this HEAD")
            inputs = report["artifact_hashes"]
            check(str(script.relative_to(ROOT).as_posix()) in inputs,
                  f"G21-REGRESSION-INPUTS-G{number:02d}", "report binds the acceptance test")
            for relative, digest in inputs.items():
                path = ROOT / relative
                if path.parts and relative.split("/")[0] not in {"build", "logs", ".toolchain"}:
                    check(path.is_file() and sha256(path) == digest,
                          f"G21-REUSE-G{number:02d}-{relative}", "recorded input bytes are unchanged")
            continue
        result = subprocess.run([sys.executable, str(script)], cwd=ROOT, text=True, encoding="utf-8",
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
        check(result.returncode == 0, f"G21-REGRESSION-G{number:02d}",
              f"G{number:02d} acceptance test passes" if result.returncode == 0 else result.stdout[-2000:])

    progress = (ROOT / "docs" / "G21_PROGRESS.md").read_text(encoding="utf-8")
    for needle in ("Not implemented", "HARDWARE_NOT_VERIFIED", "compatibility-pending",
                   "symmetric", "entropy", "metadata-only"):
        check(needle in progress, f"G21-DOC-{needle.upper().replace(' ', '-')}", f"progress record states '{needle}'")
    print("G21 software acceptance checks complete; hardware remains HARDWARE_NOT_VERIFIED.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL G21: {exc}", file=sys.stderr)
        raise SystemExit(1)
