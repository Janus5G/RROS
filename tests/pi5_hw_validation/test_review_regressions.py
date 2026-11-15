#!/usr/bin/env python3
"""Targeted CR-01--CR-07 software regressions. No device access or hardware claims."""
from __future__ import annotations

import argparse
import contextlib
import hashlib
import io
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path[:0] = [str(HERE), str(ROOT / "scripts/pi5_hw"), str(ROOT / "tools")]
import capture_serial
import build_probe_package
import build_package
import mkimage
from evaluate_probe_log import COMMON, PI5_ONLY, evaluate

QEMU = (HERE / "fixtures/qemu_probe.serial.log").read_text(encoding="utf-8")


def synthetic_pi_log(address: str = "0x107D001000") -> str:
    return "\n".join([
        "# SYNTHETIC PARSER FIXTURE. NOT PHYSICAL HARDWARE EVIDENCE.",
        "PI5PROBE BEGIN revision=synthetic",
        "PI5PROBE DT parse=1 root_compatible=raspberrypi,5-model-b",
        f"PI5PROBE UART resolved=1 stdout_path=serial0 base={address}",
        *(f"PI5PROBE CHECK name={name} result=PASS" for name in COMMON + PI5_ONLY),
        "PI5PROBE END pass=18 fail=0 result=DIAGNOSTICS-COMPLETE physical_claims=none",
        "",
    ])


def copy_files(root: Path, names: list[str]) -> None:
    for name in names:
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / name, target)


def checksums(root: Path) -> None:
    lines = [f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.relative_to(root).as_posix()}\n"
             for path in sorted(root.rglob("*")) if path.is_file() and path.name != "PACKAGE_SHA256SUMS"]
    (root / "PACKAGE_SHA256SUMS").write_text("".join(lines), encoding="utf-8")


def command(root: Path, script: str, *args: str) -> subprocess.CompletedProcess[str]:
    env = {key: value for key, value in os.environ.items() if key not in {"PYTHONPATH", "PYTHONHOME"}}
    return subprocess.run([sys.executable, str(root / script), *args], cwd=root,
                          env=env, capture_output=True, text=True, timeout=30)


def probe_package(root: Path) -> Path:
    # Exercise the real producer with explicitly synthetic, non-bootable firmware.
    copy_files(root, build_probe_package.EXTRA)
    pins = json.loads((root / "scripts/pi5_hw/boot_chain_pins.json").read_text())
    cache = root / "build/pi5-boot-chain-cache"
    cache.mkdir(parents=True)
    archive = cache / pins["zip"]["name"]
    with zipfile.ZipFile(archive, "w") as output:
        for name, meta in pins["files"].items():
            data = f"SYNTHETIC TOOL FIXTURE: {name}\n".encode()
            output.writestr(name, data)
            meta.update(size=len(data), sha256=hashlib.sha256(data).hexdigest())
    pins["zip"].update(size=archive.stat().st_size, sha256=hashlib.sha256(archive.read_bytes()).hexdigest())
    (root / "scripts/pi5_hw/boot_chain_pins.json").write_text(json.dumps(pins), encoding="utf-8")
    app = root / "build/pi5-probe/esp/EFI/BOOT/BOOTAA64.EFI"
    app.parent.mkdir(parents=True)
    app.write_bytes(b"SYNTHETIC TOOL FIXTURE, NOT A BOOTABLE EFI")
    log = root / "logs/pi5_probe/fixture_qemu_probe_serial.log"
    log.parent.mkdir(parents=True)
    log.write_text(QEMU, encoding="utf-8")
    manifest = root / "build/manifests/SHA256SUMS"
    manifest.parent.mkdir(parents=True)
    manifest.write_text("", encoding="utf-8")
    result = command(root, "scripts/pi5_hw/build_probe_package.py")
    if result.returncode:
        raise AssertionError(result.stdout + result.stderr)
    return root / "build/pi5-probe-package"


class CR01(unittest.TestCase):
    def test_labels_reject_escape_before_device_or_output_access(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "not-created"
            for label in ("../../escape", "../x", r"..\escape", "/absolute", r"C:\escape",
                          "", ".", "..", "a/b", "a\\b", "a b", "a\nb", "\u00e6", "a" * 65):
                with self.subTest(label=label), patch.object(sys, "argv", [
                    "capture_serial.py", "--port", "MUST-NOT-OPEN", "--out", str(out), "--label", label,
                ]), patch.dict(sys.modules, {"serial": None}), contextlib.redirect_stderr(io.StringIO()):
                    with self.assertRaises(SystemExit) as error:
                        capture_serial.main()
                    self.assertEqual(error.exception.code, 2)
                    self.assertFalse(out.exists())

    def test_safe_capture_stays_inside_selected_directory(self) -> None:
        import serial
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "captures"
            with patch.object(sys, "argv", ["capture_serial.py", "--port", "FAKE", "--seconds", "0",
                                           "--out", str(out), "--label", "Pi5-probe_01"]), \
                    patch.object(serial, "Serial") as device, contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(capture_serial.main(), 0)
                device.return_value.__enter__.return_value.read.assert_not_called()
            files = list(out.iterdir())
            self.assertEqual(len(files), 2)
            self.assertTrue(all(path.resolve().parent == out.resolve() for path in files))


class CR02(unittest.TestCase):
    def test_copied_probe_builder_is_standalone_and_read_only(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            package = probe_package(base / "source")
            standalone = base / "standalone package"
            shutil.copytree(package, standalone)
            shutil.rmtree(base / "source")
            before = (standalone / "PACKAGE_SHA256SUMS").read_bytes()
            for args in ((), ("--verify",)):
                result = command(standalone, "scripts/pi5_hw/build_probe_package.py", *args)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("PASS PACKAGE-INTEGRITY", result.stdout)
            self.assertFalse((standalone / "build").exists())
            self.assertEqual((standalone / "PACKAGE_SHA256SUMS").read_bytes(), before)
            (standalone / "card/EFI/BOOT/BOOTAA64.EFI").write_bytes(b"tampered")
            result = command(standalone, "scripts/pi5_hw/build_probe_package.py", "--verify")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("checksum mismatch", result.stderr)


class CR03(unittest.TestCase):
    def test_copied_legacy_builder_imports_and_verifies_real_image_structure(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            source = base / "source"
            copy_files(source, build_package.PACKAGE_FILES)
            # Minimal structural PE fixture, not executable or physical evidence.
            app = bytearray(256)
            app[:2] = b"MZ"
            struct.pack_into("<I", app, 0x3C, 64)
            app[64:68] = b"PE\0\0"
            struct.pack_into("<H", app, 68, 0xAA64)
            struct.pack_into("<H", app, 64 + 24 + 68, 10)
            current = source / "build/aarch64/esp/EFI/BOOT/BOOTAA64.EFI"
            current.parent.mkdir(parents=True)
            current.write_bytes(app)
            image = source / "build/aarch64/ExperimentalOS-Pi5.img"
            fat = mkimage.make_fat({"EFI": {"BOOT": {"BOOTAA64.EFI": bytes(app)}}}, 2 * 1024 * 1024, 12, hidden=2048)
            image.write_bytes(mkimage.make_gpt_disk(fat, 8192))
            manifest = source / "build/manifests/SHA256SUMS"
            manifest.parent.mkdir(parents=True)
            manifest.write_text("".join(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.relative_to(source).as_posix()}\n"
                                        for path in (image, current)), encoding="utf-8")
            result = command(source, "scripts/pi5_hw/build_package.py")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            standalone = base / "standalone validation"
            shutil.copytree(source / "build/pi5-hw-validation", standalone)
            shutil.rmtree(source)
            script = "package/scripts/pi5_hw/build_package.py"
            for args in ((), ("--verify",)):
                result = command(standalone, script, *args)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("PASS STANDALONE-IMAGE", result.stdout)
            self.assertFalse((standalone / "build").exists())
            (standalone / "release-snapshot/BOOTAA64.EFI.from-image").write_bytes(b"tampered")
            checksums(standalone)
            result = command(standalone, script, "--verify")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("boot application in image differs", result.stderr)
            (standalone / "PACKAGE_SHA256SUMS").write_text("0" * 64 + "  ../escape\n", encoding="utf-8")
            result = command(standalone, script, "--verify")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Unsafe", result.stderr)


class CR04(unittest.TestCase):
    def test_end_requires_complete_valid_consistent_result(self) -> None:
        prefix = synthetic_pi_log().split("PI5PROBE END")[0]
        self.assertEqual(evaluate(synthetic_pi_log(), "pi5"), ("PI5_PROBE_PASS", []))
        for end in ("", "PI5PROBE END pass=18 fail=0 result=", "PI5PROBE END pass=18 fail=0 result=DIAGNOSTICS-",
                    "PI5PROBE END pass=18 fail=0 result=PASS", "PI5PROBE END pass=18 fail=0 result=DIAGNOSTICS-COMPLETEX",
                    "PI5PROBE END pass=18 fail=0 result=DIAGNOSTICS-FAILURES",
                    "PI5PROBE END pass=19 fail=0 result=DIAGNOSTICS-COMPLETE"):
            with self.subTest(end=end):
                self.assertEqual(evaluate(prefix + end + "\n", "pi5")[0], "FAIL")
        self.assertEqual(evaluate(synthetic_pi_log() + "PI5PROBE END pass=18 fail=0 result=\n", "pi5")[0], "FAIL")


class CR05(unittest.TestCase):
    def test_valid_hex_formats_and_wrong_or_malformed_addresses(self) -> None:
        for address in ("0x107d001000", "0x107D001000", "0X107D001000", "0x000000107D001000"):
            with self.subTest(address=address):
                self.assertEqual(evaluate(synthetic_pi_log(address), "pi5"), ("PI5_PROBE_PASS", []))
        for address in ("0x09000000", "0x107d001001", "0x107d001000g", "0x107d001000/extra",
                        "0x", "-0x107d001000", "0x1000000107d001000"):
            with self.subTest(address=address):
                self.assertEqual(evaluate(synthetic_pi_log(address), "pi5")[0], "FAIL")
        self.assertEqual(evaluate(synthetic_pi_log() + "PI5PROBE UART resolved=1 base=0xBADjunk\n", "pi5")[0], "FAIL")


class CR06(unittest.TestCase):
    def test_generated_package_contains_and_runs_documented_commands(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            package = probe_package(Path(tmp) / "source")
            for script, args, expected in (
                ("scripts/pi5_hw/build_probe_package.py", ("--verify",), "PASS PACKAGE-INTEGRITY"),
                ("tests/pi5_hw_validation/selftest.py", (), "PASS capture_serial.py --list"),
                ("tests/pi5_hw_validation/evaluate_probe_log.py", ("--mode", "qemu", "qemu_probe_serial.log"), "SOFTWARE_PASS"),
                ("tests/pi5_hw_validation/capture_serial.py", ("--list",), ""),
            ):
                with self.subTest(script=script):
                    self.assertTrue((package / script).is_file())
                    result = command(package, script, *args)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn(expected, result.stdout)
            self.assertTrue((package / "tests/pi5_hw_validation/evaluate_serial_log.py").is_file())
            status = (package / "STATUS.md").read_text()
            self.assertIn("evaluate_probe_log.py --mode qemu", status)
            self.assertIn("selftest.py", status)


class CR07(unittest.TestCase):
    def test_duplicates_conflicts_and_missing_markers(self) -> None:
        text = synthetic_pi_log()
        self.assertEqual(evaluate(text + text, "pi5"), ("PI5_PROBE_PASS", []))
        failed = f"PI5PROBE CHECK name={COMMON[0]} result=FAIL\n"
        for conflict in (failed + text, text + failed):
            self.assertEqual(evaluate(conflict, "pi5")[0], "FAIL")
        for marker in ("PI5PROBE BEGIN", "PI5PROBE END", "PI5PROBE UART", "PI5PROBE DT",
                       *(f"PI5PROBE CHECK name={name} " for name in COMMON + PI5_ONLY)):
            with self.subTest(marker=marker):
                partial = "\n".join(line for line in text.splitlines() if not line.startswith(marker))
                self.assertEqual(evaluate(partial, "pi5")[0], "FAIL")
        self.assertEqual(evaluate(text + "PI5PROBE END pass=0 fail=18 result=DIAGNOSTICS-FAILURES\n", "pi5")[0], "FAIL")

    def test_identity_mismatch_and_authentic_qemu_sample(self) -> None:
        text = synthetic_pi_log()
        for identity in ("raspberrypi,4-model-b", "raspberrypi,50", "linux,dummy-virt"):
            with self.subTest(identity=identity):
                wrong = text.replace("raspberrypi,5-model-b", identity) + "# mentioned raspberrypi,5 elsewhere\n"
                self.assertEqual(evaluate(wrong, "pi5")[0], "FAIL")
        self.assertEqual(evaluate(QEMU, "qemu"), ("SOFTWARE_PASS", []))
        self.assertEqual(evaluate(QEMU + QEMU, "qemu"), ("SOFTWARE_PASS", []))
        self.assertEqual(evaluate(QEMU, "pi5")[0], "FAIL")
        self.assertEqual(evaluate("", "qemu")[0], "FAIL")
        self.assertEqual(evaluate(QEMU.replace(f"name={COMMON[0]} result=PASS",
                                            f"name={COMMON[0]} result=FAIL"), "qemu")[0], "FAIL")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--finding", choices=[f"CR{number:02d}" for number in range(1, 8)])
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    findings = [args.finding] if args.finding else [f"CR{number:02d}" for number in range(1, 8)]
    reports = []
    for name in findings:
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(globals()[name])
        result = unittest.TextTestRunner(verbosity=2).run(suite)
        status = "FAIL" if not result.wasSuccessful() else ("NOT_TESTABLE" if result.skipped or not result.testsRun else "PASS")
        reports.append({"finding": name.replace("CR", "CR-"), "status": status, "tests_run": result.testsRun,
                        "failures": [(str(test), detail) for test, detail in result.failures + result.errors]})
    report = {
        "source_commit": subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, check=True, capture_output=True, text=True).stdout.strip(),
        "source_tree_dirty": bool(subprocess.run(
            ["git", "status", "--porcelain", "--", "scripts/pi5_hw", "tests/pi5_hw_validation"],
            cwd=ROOT, check=True, capture_output=True, text=True).stdout),
        "input_sha256": {
            name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
            for name in (
                "scripts/pi5_hw/build_package.py", "scripts/pi5_hw/build_probe_package.py",
                "scripts/pi5_hw/package_support.py", "tests/pi5_hw_validation/capture_serial.py",
                "tests/pi5_hw_validation/evaluate_probe_log.py",
                "tests/pi5_hw_validation/test_review_regressions.py",
            ) if (ROOT / name).is_file()
        },
        "findings": reports, "hardware_status": "HARDWARE_NOT_VERIFIED",
        "fixtures": {"pi5": "SYNTHETIC; not physical evidence", "package_images": "SYNTHETIC; not bootable",
                     "qemu_log": "authentic marker excerpt; generic QEMU only",
                     "qemu_sha256": hashlib.sha256(QEMU.encode()).hexdigest()},
    }
    print(json.dumps(report, indent=2))
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 0 if all(item["status"] == "PASS" for item in reports) else 1


if __name__ == "__main__":
    raise SystemExit(main())
