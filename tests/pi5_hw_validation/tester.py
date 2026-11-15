#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RROS contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Portable RROS evidence collection. Never flashes media, drives GPIO or sends email."""
from __future__ import annotations

import argparse
import hashlib
import json
import platform
import subprocess
import sys
import tempfile
import zipfile
from email.message import EmailMessage
from email.headerregistry import Address
from pathlib import Path

from evaluate_probe_log import COMMON, evaluate

SUBSYSTEMS = ("pcie-rp1", "usb", "ethernet", "display", "camera", "audio", "wireless", "storage")
LIMIT = 64 * 1024 * 1024


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def input_file(path: Path) -> Path:
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"Expected a regular, non-symlink file: {path}")
    return path


def small_bytes(path: Path) -> bytes:
    input_file(path)
    if path.stat().st_size > LIMIT:
        raise ValueError(f"Input exceeds the 64 MiB limit: {path}")
    return path.read_bytes()


def write_json(path: Path, value: object) -> None:
    with path.open("x", encoding="utf-8") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.write("\n")


def collect(args: argparse.Namespace) -> int:
    artifact = input_file(args.artifact)
    artifact_hash = digest(artifact)
    if artifact_hash.lower() != args.expected_sha256.lower():
        raise ValueError("Artifact SHA-256 does not match the tester-supplied expected value")
    if args.out.exists():
        raise ValueError(f"Use a new output directory, not existing evidence: {args.out}")
    if args.log:
        raw = small_bytes(args.log)
    else:
        if not args.port or not 1 <= args.seconds <= 3600:
            raise ValueError("Supply --log or --port and a capture duration of 1-3600 seconds")
        with tempfile.TemporaryDirectory(prefix="rros-uart-") as temporary:
            subprocess.run(
                [sys.executable, str(Path(__file__).with_name("capture_serial.py")),
                 "--port", args.port, "--seconds", str(args.seconds),
                 "--out", temporary, "--label", "probe"], check=True)
            logs = list(Path(temporary).glob("*.serial.log"))
            if len(logs) != 1:
                raise ValueError("Capture did not produce exactly one UART log")
            raw = small_bytes(logs[0])
    observations = {
        name: {"status": "NOT_TESTABLE", "reason": "No native physical test submitted"}
        for name in SUBSYSTEMS
    }
    if args.observations:
        supplied = json.loads(small_bytes(args.observations).decode("utf-8"))
        if not isinstance(supplied, dict) or set(supplied) != set(SUBSYSTEMS):
            raise ValueError(f"Observations must contain exactly: {', '.join(SUBSYSTEMS)}")
        for name, item in supplied.items():
            if (not isinstance(item, dict) or item.get("status") not in
                    {"PASS", "FAIL", "NOT_TESTABLE", "HARDWARE_NOT_VERIFIED"}
                    or not isinstance(item.get("reason"), str) or not item["reason"].strip()):
                raise ValueError(f"Invalid status or missing evidence/reason for {name}")
        observations = supplied
    verdict, problems = evaluate(raw.decode("utf-8", errors="replace"), args.mode)
    args.out.mkdir(parents=True)
    (args.out / "uart.serial.log").write_bytes(raw)
    write_json(args.out / "results.json", {
        "schema": 1, "mode": args.mode, "synthetic": args.synthetic,
        "board_revision": args.board_revision, "firmware": args.firmware,
        "release": args.release, "artifact_name": artifact.name,
        "artifact_sha256": artifact_hash,
        "uart_sha256": hashlib.sha256(raw).hexdigest(),
        "parser_verdict": verdict, "parser_problems": problems,
        "hardware_verification": "HARDWARE_NOT_VERIFIED",
        "boundary": "Parser acceptance is not authentication or independent board verification.",
    })
    write_json(args.out / "subsystems.json", observations)
    write_json(args.out / "environment.json", {
        "python": platform.python_version(), "system": platform.system(),
        "machine": platform.machine(), "serial_baud": 115200,
    })
    files = sorted(args.out.iterdir())
    (args.out / "SHA256SUMS").write_text(
        "".join(f"{digest(path)}  {path.name}\n" for path in files), encoding="ascii")
    files.append(args.out / "SHA256SUMS")
    archive = args.out / "report.zip"
    with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED) as output:
        for path in files:
            info = zipfile.ZipInfo(path.name, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            output.writestr(info, path.read_bytes())
    (args.out / "report.zip.sha256").write_text(f"{digest(archive)}  report.zip\n", encoding="ascii")
    print(f"{verdict}; HARDWARE_NOT_VERIFIED; report={archive}")
    return 1 if problems else 0


def draft(args: argparse.Namespace) -> int:
    if not args.approve:
        raise ValueError("Review/redact the archive, then explicitly approve with --approve; nothing sent")
    if "\r" in args.to or "\n" in args.to:
        raise ValueError("Recipient must not contain header line breaks")
    recipient = Address(addr_spec=args.to)
    if not recipient.domain or not recipient.username:
        raise ValueError("Supply the owner-provided recipient address")
    data = small_bytes(args.archive)
    if not zipfile.is_zipfile(args.archive):
        raise ValueError("Expected a report ZIP archive")
    message = EmailMessage()
    message["To"] = recipient
    message["Subject"] = "RROS Pi 5 validation report — tester-reviewed draft"
    message.set_content(
        "Tester-approved draft only. No email has been sent.\n"
        f"Attached report SHA-256: {hashlib.sha256(data).hexdigest()}\n"
        "Please distinguish baseline, firmware, probe and native observations.\n")
    message.add_attachment(data, maintype="application", subtype="zip", filename="report.zip")
    with args.out.open("xb") as stream:
        stream.write(message.as_bytes())
    print(f"DRY_RUN: email draft={args.out}; nothing sent. Open/review and send manually if approved.")
    return 0


def demo(args: argparse.Namespace) -> int:
    with tempfile.TemporaryDirectory(prefix="rros-demo-") as temporary:
        root = Path(temporary)
        log = root / "synthetic.log"
        log.write_text(
            "# SYNTHETIC SOFTWARE DEMO — NOT PHYSICAL EVIDENCE\nPI5PROBE BEGIN\n"
            "PI5PROBE UART resolved=1\n" +
            "\n".join(f"PI5PROBE CHECK name={name} result=PASS" for name in COMMON) +
            f"\nPI5PROBE END pass={len(COMMON)} fail=0 result=DIAGNOSTICS-COMPLETE\n",
            encoding="utf-8")
        artifact = root / "synthetic-not-bootable.txt"
        artifact.write_bytes(b"SYNTHETIC SOFTWARE DEMO, NOT AN EFI IMAGE\n")
        return collect(argparse.Namespace(
            artifact=artifact, expected_sha256=digest(artifact), out=args.out,
            log=log, port=None, observations=None, mode="qemu", synthetic=True,
            board_revision="none; synthetic", firmware="none; synthetic",
            release="software-demo", seconds=1))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    capture = commands.add_parser("collect", help="Capture/import UART and assemble a report")
    source = capture.add_mutually_exclusive_group(required=True)
    source.add_argument("--log", type=Path)
    source.add_argument("--port")
    capture.add_argument("--seconds", type=int, default=180)
    capture.add_argument("--mode", choices=("pi5", "qemu"), required=True)
    for field in ("board-revision", "firmware", "release"):
        capture.add_argument(f"--{field}", required=True)
    capture.add_argument("--artifact", type=Path, required=True)
    capture.add_argument("--expected-sha256", required=True)
    capture.add_argument("--observations", type=Path)
    capture.add_argument("--out", type=Path, required=True)
    capture.set_defaults(handler=collect, synthetic=False)
    email = commands.add_parser("email-draft", help="Approval-gated local .eml assistant; no transmission")
    email.add_argument("--archive", type=Path, required=True)
    email.add_argument("--to", required=True)
    email.add_argument("--approve", action="store_true")
    email.add_argument("--out", type=Path, required=True)
    email.set_defaults(handler=draft)
    example = commands.add_parser("demo", help="Exercise the pipeline with clearly synthetic software input")
    example.add_argument("--out", type=Path, required=True)
    example.set_defaults(handler=demo)
    args = parser.parse_args()
    try:
        return args.handler(args)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
