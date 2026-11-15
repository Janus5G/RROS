#!/usr/bin/env python3
"""Focused regressions for deterministic public archives and evidence separation."""
from __future__ import annotations

import importlib.util
import hashlib
import json
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/pi5_hw_validation"))
from evaluate_probe_log import COMMON, evaluate

spec = importlib.util.spec_from_file_location("prepare_release", ROOT / "scripts/release/prepare.py")
assert spec and spec.loader
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)
verify_spec = importlib.util.spec_from_file_location("verify_release", ROOT / "scripts/release/verify.py")
assert verify_spec and verify_spec.loader
verifier = importlib.util.module_from_spec(verify_spec)
verify_spec.loader.exec_module(verifier)
arm_spec = importlib.util.spec_from_file_location("arm_release", ROOT / "scripts/run_arm64_ci.py")
assert arm_spec and arm_spec.loader
arm_runner = importlib.util.module_from_spec(arm_spec)
arm_spec.loader.exec_module(arm_runner)
g21_spec = importlib.util.spec_from_file_location("release_g21", ROOT / "tests/gates/G21_test.py")
assert g21_spec and g21_spec.loader
g21 = importlib.util.module_from_spec(g21_spec)
g21_spec.loader.exec_module(g21)


class ReleaseTests(unittest.TestCase):
    def test_archive_is_deterministic_and_safe(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "input"
            source.write_bytes(b"authentic fixture input")
            first, second = root / "one.tar.gz", root / "two.tar.gz"
            release.archive(first, {"EFI/BOOT/BOOTAA64.EFI": source})
            release.archive(second, {"EFI/BOOT/BOOTAA64.EFI": source})
            self.assertEqual(first.read_bytes(), second.read_bytes())
            with tarfile.open(first) as tar:
                member = tar.getmember("EFI/BOOT/BOOTAA64.EFI")
                self.assertEqual(member.mtime, 1767225600)
                stream = tar.extractfile(member)
                assert stream
                self.assertEqual(stream.read(), source.read_bytes())
            link = root / "link"
            link.symlink_to(source)
            with self.assertRaisesRegex(ValueError, "symlink"):
                release.archive(root / "bad.tar.gz", {"link": link})

    def test_snapshot_ignores_caches_but_binds_source_changes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "kernel").mkdir()
            source = root / "kernel/core.c"
            source.write_text("original", encoding="ascii")
            digest = release.source_digest(root)
            (root / "kernel/__pycache__").mkdir()
            (root / "kernel/__pycache__/cache.pyc").write_bytes(b"cache")
            self.assertEqual(digest, release.source_digest(root))
            source.write_text("changed", encoding="ascii")
            self.assertNotEqual(digest, release.source_digest(root))

    def test_qemu_probe_is_not_physical_evidence(self) -> None:
        text = "PI5PROBE BEGIN\nPI5PROBE UART resolved=1\n"
        text += "\n".join(f"PI5PROBE CHECK name={name} result=PASS" for name in COMMON)
        text += "\nPI5PROBE END pass=16 fail=0 result=DIAGNOSTICS-COMPLETE\n"
        self.assertEqual(evaluate(text, "qemu")[0], "SOFTWARE_PASS")
        verdict, reasons = evaluate(text, "pi5")
        self.assertEqual(verdict, "FAIL")
        self.assertTrue(any("Raspberry Pi 5" in reason for reason in reasons))
        self.assertEqual(evaluate("", "qemu")[0], "FAIL")

    def test_tooling_only_and_tampered_manifests_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = root / "release-manifest.json"
            manifest.write_text(json.dumps({"software_status": "NOT_TESTABLE"}), encoding="utf-8")
            digest = hashlib.sha256(manifest.read_bytes()).hexdigest()
            (root / "SHA256SUMS").write_text(f"{digest}  release-manifest.json\n", encoding="ascii")
            with self.assertRaisesRegex(ValueError, "not a full software PASS"):
                verifier.verify(root)
            manifest.write_text("tampered", encoding="ascii")
            with self.assertRaisesRegex(ValueError, "Checksum mismatch"):
                verifier.verify(root)

    def test_hosted_arm_does_not_execute_privileged_driver_suite(self) -> None:
        with patch.object(arm_runner.platform, "machine", return_value="aarch64"), \
                patch.object(arm_runner, "write_evidence") as evidence, \
                patch.object(arm_runner.subprocess, "run") as run:
            run.side_effect = lambda command, **kwargs: subprocess.CompletedProcess(
                command, 0, stdout="PASS\n", stderr="")
            self.assertEqual(arm_runner.main(), 0)
            self.assertFalse(any("test-pi5-host.sh" in str(call.args) for call in run.call_args_list))
            report = evidence.call_args.args[0]
            self.assertTrue(any(r["status"] == "NOT_TESTABLE" and "G12" in r["name"]
                                for r in report["results"]))

    def test_host_only_audit_does_not_allow_libc_in_runtime(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for directory in ("kernel", "arch", "boot", "scripts"):
                (root / directory).mkdir()
            (root / "kernel/boot_console_test.c").write_text("#include <stdio.h>\n", encoding="ascii")
            (root / "scripts/test-boot-console-host.sh").write_text("kernel/boot_console_test.c\n", encoding="ascii")
            for name in ("build-x86_64.sh", "build-aarch64.sh", "build-pi5-probe.sh"):
                (root / "scripts" / name).write_text("kernel/boot_console.c\n", encoding="ascii")
            with patch.object(g21, "ROOT", root):
                g21.dependency_audit()
                (root / "kernel/core.c").write_text("#include <stdio.h>\n", encoding="ascii")
                with self.assertRaisesRegex(AssertionError, "core.c-stdio.h"):
                    g21.dependency_audit()


if __name__ == "__main__":
    unittest.main()
