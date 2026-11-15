#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RROS contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Focused community-delivery reproducibility, safety and PDF regressions."""
from __future__ import annotations

import importlib.util
import io
import json
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile
from contextlib import redirect_stdout
from pathlib import Path

from pypdf import PdfReader

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("community", ROOT / "scripts/release/community.py")
assert spec and spec.loader
community = importlib.util.module_from_spec(spec)
spec.loader.exec_module(community)


class CommunityTests(unittest.TestCase):
    def test_analyzer_refuses_success_without_path_analysis(self) -> None:
        spec = importlib.util.spec_from_file_location(
            "community_safety", ROOT / "scripts/release/community_safety.py")
        assert spec and spec.loader
        analyzer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(analyzer)
        completed = subprocess.CompletedProcess(
            [], 0, stdout="", stderr="Every top-level function was skipped.\n")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = io.StringIO()
            with patch.object(analyzer, "ROOT", root), \
                    patch.object(analyzer.subprocess, "run", return_value=completed), \
                    redirect_stdout(output):
                self.assertEqual(analyzer.main(), 1)
            self.assertIn("FAIL", output.getvalue())
            report = json.loads((root / "build/community-checks/static-analysis.json").read_bytes())
            self.assertTrue(report["results"])
            self.assertTrue(all(r["status"] == "FAIL" and not r["path_analysis_executed"]
                                for r in report["results"]))

    def test_source_exclusions_and_archive_integrity(self) -> None:
        for forbidden in ("OSBuild.pdf", "build/image.img", "tests/gates/fixtures/roboforge/x.json",
                          "docs/site/desktop.html", ".github/agents/private.md",
                          "scripts/__pycache__/x.pyc"):
            self.assertFalse(community.selected(forbidden), forbidden)
        self.assertTrue(community.selected("kernel/memory.c"))
        self.assertTrue(community.selected("tests/pi5_hw_validation/fixtures/qemu_probe.serial.log"))
        files = {"kernel/test.c": b"original source\n", "LICENSE": b"test license\n"}
        first = community.zip_bytes(files)
        self.assertEqual(first, community.zip_bytes(files))
        community.verify_zip(first)
        damaged = io.BytesIO()
        with zipfile.ZipFile(io.BytesIO(first)) as original, zipfile.ZipFile(damaged, "w") as output:
            for name in original.namelist():
                output.writestr(name, b"tampered" if name.endswith(".c") else original.read(name))
        with self.assertRaisesRegex(ValueError, "checksum mismatch"):
            community.verify_zip(damaged.getvalue())

    def test_offline_kit_refuses_ports_and_preserves_reports(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for target, source in {
                "community_tester.py": "scripts/pi5_hw/community_tester.py",
                "tester.py": "tests/pi5_hw_validation/tester.py",
                "evaluate_probe_log.py": "tests/pi5_hw_validation/evaluate_probe_log.py",
            }.items():
                (root / target).write_bytes((ROOT / source).read_bytes())
            command = [sys.executable, str(root / "community_tester.py")]
            denied = subprocess.run(command + ["collect", "--port", "INVALID"],
                                    capture_output=True, text=True)
            self.assertEqual(denied.returncode, 2)
            self.assertIn("BLOCKED", denied.stderr)
            output = root / "report"
            demo = subprocess.run(command + ["demo", "--out", str(output)],
                                  capture_output=True, text=True)
            self.assertEqual(demo.returncode, 0, demo.stdout + demo.stderr)
            report = json.loads((output / "results.json").read_bytes())
            self.assertTrue(report["synthetic"])
            self.assertEqual(report["hardware_verification"], "HARDWARE_NOT_VERIFIED")
            with zipfile.ZipFile(output / "report.zip") as archive:
                self.assertIn("uart.serial.log", archive.namelist())
                self.assertIn("SHA256SUMS", archive.namelist())
            repeat = subprocess.run(command + ["demo", "--out", str(output)],
                                    capture_output=True, text=True)
            self.assertNotEqual(repeat.returncode, 0)

    def test_pdf_is_readable_complete_and_deterministic(self) -> None:
        guide = (ROOT / "docs/community/HARDWARE_GUIDE.md").read_text(encoding="utf-8")
        bom = (ROOT / "docs/community/BOM.csv").read_text(encoding="utf-8")
        pdf = community.pdf_guide(guide, "a" * 40, bom)
        self.assertEqual(pdf, community.pdf_guide(guide, "a" * 40, bom))
        reader = PdfReader(io.BytesIO(pdf), strict=True)
        self.assertGreaterEqual(len(reader.pages), 8)
        text = "\n".join(page.extract_text() for page in reader.pages)
        for required in ("Shopping list", "Numbered assembly-review", "5.1 V / 5 A",
                         "microSD reader", "DISCONNECTED", "NOT PHYSICAL TEST APPROVAL",
                         "Test gates", "stop conditions", "NOT_VERIFIED"):
            self.assertIn(required, text)
        gate = json.loads((ROOT / "docs/community/SAFETY_GATE.json").read_bytes())
        self.assertEqual(gate["physical_operations_approved"], [])
        self.assertIn("power-up", gate["physical_operations_blocked"])
        for requirement in gate["requirements"]:
            self.assertIn(requirement["status"], {"PASS", "FAIL", "BLOCKED", "NOT_VERIFIED"})
            if requirement["id"] in {"C17-MAP", "C17-UART", "OWNERSHIP-CONCURRENCY"}:
                self.assertEqual(requirement["status"], "PASS")
        for physical in ("TARGET-ENTRY", "MMIO", "IRQ-DMA", "ELECTRICAL", "FIRMWARE-MEDIA"):
            requirement = next(r for r in gate["requirements"] if r["id"] == physical)
            self.assertIn(requirement["status"], {"BLOCKED", "NOT_VERIFIED"})
            self.assertTrue(requirement["critical"])
            self.assertTrue(requirement["blocks"])
        checklist = (ROOT / "docs/community/ELECTRICAL_PRE_POWER.md").read_text(encoding="utf-8")
        self.assertIn("NOT_VERIFIED", checklist)
        self.assertIn("polarity", checklist)
        self.assertIn("current", checklist)


if __name__ == "__main__":
    unittest.main()
