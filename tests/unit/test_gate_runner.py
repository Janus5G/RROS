#!/usr/bin/env python3
"""Focused tests for UTF-8 gate output and duplicate evidence handling."""

from __future__ import annotations

import json
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts" / "gates"))

import run_gate


def main() -> int:
    report = {
        "date": "2026-11-11T00:00:00+00:00",
        "source_commit": "abc123",
        "artifact_hashes": {"kernel/pi5_driver.c": "0" * 64},
        "actual_results": "æøå — “quoted”",
    }
    shifted_date = dict(report, date="2026-11-11T01:00:00+00:00")
    changed_output = dict(report, actual_results="different")
    assert run_gate.evidence_fingerprint(report) == run_gate.evidence_fingerprint(
        shifted_date
    )
    assert run_gate.evidence_fingerprint(report) != run_gate.evidence_fingerprint(
        changed_output
    )
    assert run_gate.source_tree_state_from_status("") == "CLEAN"
    assert run_gate.source_tree_state_from_status(" M kernel/pi5_driver.c") == "DIRTY"

    with tempfile.TemporaryDirectory(prefix="gate-runner-test-") as directory:
        log_dir = Path(directory)
        history = log_dir / "history"
        json_path = log_dir / "G12_RESULT.json"
        json_path.write_text(
            json.dumps(report, ensure_ascii=False), encoding="utf-8"
        )
        (log_dir / "G12_RESULT.md").write_text(
            "# æøå “quoted”\n", encoding="utf-8"
        )
        (log_dir / "G12_SHA256.txt").write_text(
            "0" * 64 + "  logs/G12_RESULT.json\n", encoding="ascii"
        )
        run_gate.archive_previous_results(log_dir, "G12")
        assert len(list(history.iterdir())) == 3
        run_gate.archive_previous_results(log_dir, "G12")
        assert len(list(history.iterdir())) == 3

        updated_report = dict(report, actual_results="different")
        json_path.write_text(
            json.dumps(updated_report, ensure_ascii=False), encoding="utf-8"
        )
        run_gate.archive_previous_results(log_dir, "G12")
        assert len(list(history.iterdir())) == 6
        run_gate.archive_previous_results(log_dir, "G12")
        assert len(list(history.iterdir())) == 6

    with tempfile.TemporaryDirectory(prefix="gate-runner-manifest-") as directory:
        history = Path(directory)
        assert run_gate.should_archive_previous(history, "G12", report)
        run_id = "20261111T000000000000Z"
        stem = history / f"G12_{run_id}_G12_RESULT"
        stem.with_suffix(".json").write_text(
            json.dumps(shifted_date), encoding="utf-8"
        )
        stem.with_suffix(".md").write_text("result\n", encoding="utf-8")
        (history / f"G12_{run_id}_G12_SHA256.txt").write_text(
            "0" * 64 + "  logs/G12_RESULT.json\n", encoding="ascii"
        )
        assert not run_gate.should_archive_previous(
            history, "G12", report
        )
        (history / f"G12_{run_id}_G12_SHA256.txt").unlink()
        assert run_gate.should_archive_previous(history, "G12", report)

    print("PASS G00-GATE-RUNNER: UTF-8 output and semantic history deduplication")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
