#!/usr/bin/env python3
"""Run one gate, preserve evidence and update the resumable gate state."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STATE_PATH = ROOT / "build" / "gate_state.json"
LOG_DIR = ROOT / "logs"
PASS_STATES = {"SOFTWARE_PASS", "HARDWARE_PASS"}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def source_revision() -> str:
    result = subprocess.run(
        ["git", "-C", str(ROOT), "rev-parse", "HEAD"],
        text=True, encoding="utf-8", capture_output=True, check=False,
    )
    return result.stdout.strip() if result.returncode == 0 else "UNCOMMITTED"


def source_tree_state_from_status(status_output: str) -> str:
    return "DIRTY" if status_output else "CLEAN"


def source_tree_state() -> str:
    result = subprocess.run(
        ["git", "-C", str(ROOT), "status", "--porcelain"],
        text=True, encoding="utf-8", capture_output=True, check=False,
    )
    if result.returncode != 0:
        return f"UNKNOWN: {result.stderr.strip()}"
    return source_tree_state_from_status(result.stdout)


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def evidence_fingerprint(report: object) -> str | None:
    if not isinstance(report, dict):
        return None
    stable_report = dict(report)
    stable_report.pop("date", None)
    encoded = json.dumps(
        stable_report, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def historical_evidence_exists(
    history_dir: Path, gate: str, fingerprint: str
) -> bool:
    for path in history_dir.glob(f"{gate}_*_{gate}_RESULT.json"):
        try:
            report = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, json.JSONDecodeError):
            continue
        if evidence_fingerprint(report) == fingerprint:
            run_id = path.name[
                len(gate) + 1 : -len(f"_{gate}_RESULT.json")
            ]
            prefix = history_dir / f"{gate}_{run_id}_{gate}_RESULT"
            if (
                prefix.with_suffix(".json").is_file()
                and prefix.with_suffix(".md").is_file()
                and (history_dir / f"{gate}_{run_id}_{gate}_SHA256.txt").is_file()
            ):
                return True
    return False


def should_archive_previous(
    history_dir: Path, gate: str, previous_report: object
) -> bool:
    fingerprint = evidence_fingerprint(previous_report)
    return fingerprint is None or not historical_evidence_exists(
        history_dir, gate, fingerprint
    )


def archive_previous_results(log_dir: Path, gate: str) -> None:
    json_path = log_dir / f"{gate}_RESULT.json"
    md_path = log_dir / f"{gate}_RESULT.md"
    hashes_path = log_dir / f"{gate}_SHA256.txt"
    history_paths = (json_path, md_path, hashes_path)
    if not any(path.exists() for path in history_paths):
        return

    history_dir = log_dir / "history"
    previous_report = None
    if json_path.is_file():
        try:
            previous_report = json.loads(json_path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, json.JSONDecodeError):
            pass
    if not should_archive_previous(history_dir, gate, previous_report):
        return

    history_dir.mkdir(parents=True, exist_ok=True)
    run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    for path in history_paths:
        if path.exists():
            shutil.copy2(path, history_dir / f"{gate}_{run_id}_{path.name}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("gate", help="gate identifier, e.g. G00")
    parser.add_argument("--test-arg", action="append", default=[],
                        help="explicit argument passed to the gate acceptance test")
    args = parser.parse_args()
    gate = args.gate.upper()
    if len(gate) != 3 or not gate.startswith("G") or not gate[1:].isdigit():
        parser.error("gate must be G00 through G22")

    test_path = ROOT / "tests" / "gates" / f"{gate}_test.py"
    if not test_path.is_file():
        print(f"ENVIRONMENT_BLOCKED: required test is absent: {test_path}", file=sys.stderr)
        return 2

    state = json.loads(STATE_PATH.read_text(encoding="utf-8"))
    gate_state = state["gates"].get(gate)
    if gate_state is None:
        parser.error(f"{gate} is not defined in {STATE_PATH}")
    for dependency in gate_state.get("depends_on", []):
        dependency_state = state["gates"][dependency]["software_status"]
        if dependency_state not in PASS_STATES:
            print(
                f"BLOCKED: {gate} requires {dependency}=SOFTWARE_PASS; "
                f"actual={dependency_state}",
                file=sys.stderr,
            )
            return 2

    LOG_DIR.mkdir(parents=True, exist_ok=True)
    started = datetime.now(timezone.utc).isoformat()
    gate_state["software_status"] = "TESTING"
    gate_state["started_at"] = started
    state["updated_at"] = started
    write_json(STATE_PATH, state)

    command = [sys.executable, str(test_path), *args.test_arg]
    test_environment = os.environ.copy()
    test_environment["PYTHONUTF8"] = "1"
    test_environment["PYTHONIOENCODING"] = "utf-8"
    result = subprocess.run(
        command, cwd=ROOT, text=True, encoding="utf-8",
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False,
        env=test_environment,
    )
    finished = datetime.now(timezone.utc).isoformat()
    status = "SOFTWARE_PASS" if result.returncode == 0 else "SOFTWARE_FAIL"
    declared_statuses = [
        line.removeprefix("GATE_STATUS: ").strip()
        for line in result.stdout.splitlines()
        if line.startswith("GATE_STATUS: ")
    ]
    if result.returncode == 0 and declared_statuses:
        if len(declared_statuses) != 1 or declared_statuses[0] != "IN_PROGRESS":
            status = "SOFTWARE_FAIL"
            result.stdout += (
                "\nFAIL GATE-STATUS: expected one supported partial status "
                "declaration (IN_PROGRESS)\n"
            )
        else:
            status = declared_statuses[0]
    recorded_commands = [command]
    recorded_commands.extend(
        line.removeprefix("COMMAND: ").strip()
        for line in result.stdout.splitlines()
        if line.startswith("COMMAND: ")
    )
    toolchain_versions = [
        line.removeprefix("TOOLCHAIN: ").strip()
        for line in result.stdout.splitlines()
        if line.startswith("TOOLCHAIN: ")
    ]
    report = {
        "gate": gate,
        "date": finished,
        "source_commit": source_revision(),
        "source_tree_state": source_tree_state(),
        "toolchain_versions": toolchain_versions or "See test output and docs/TOOLCHAIN.md",
        "platform": platform.platform(),
        "test_environment": "Python gate runner; see test output for WSL/QEMU details",
        "test_ids": [
            line.split(":", 1)[0].removeprefix("PASS ")
            for line in result.stdout.splitlines()
            if line.startswith("PASS ")
        ],
        "commands_executed": recorded_commands,
        "exit_codes": [result.returncode],
        "expected_results": (
            "All gate-specific acceptance checks pass."
            if status in PASS_STATES
            else "The reported milestone checks pass; complete gate acceptance remains pending."
        ),
        "actual_results": result.stdout,
        "error_diagnostics": "" if result.returncode == 0 else result.stdout,
        "software_status": status,
        "hardware_status": gate_state.get("hardware_status", "HARDWARE_NOT_VERIFIED"),
        "artifact_hashes": {},
        "known_limitations": gate_state.get("limitations", []),
        "remaining_software_work": gate_state.get("remaining_software_work", []),
        "hardware_verification": gate_state.get("hardware_verification"),
        "next_gate": (
            f"G{int(gate[1:]) + 1:02d}"
            if status in PASS_STATES and gate != "G22" else None
        ),
        "blocked_next_gate": (
            f"G{int(gate[1:]) + 1:02d}"
            if status == "IN_PROGRESS" and gate != "G22" else None
        ),
    }

    gate_state["software_status"] = status
    gate_state["finished_at"] = finished
    gate_state["source_commit"] = report["source_commit"]
    gate_state["last_exit_code"] = result.returncode
    gate_state["last_result"] = f"logs/{gate}_RESULT.json"
    state["updated_at"] = finished
    write_json(STATE_PATH, state)

    tracked = subprocess.run(
        ["git", "-C", str(ROOT), "ls-files", "-z"],
        capture_output=True, check=False,
    )
    tracked_paths = [
        ROOT / entry.decode("utf-8")
        for entry in tracked.stdout.split(b"\0")
        if entry
    ]
    artifact_paths = [
        path for path in tracked_paths
        if path.is_file()
        and path.relative_to(ROOT).parts[0] not in {"build", "logs"}
        and not path.name.endswith("_SHA256.txt")
    ]
    artifact_paths.extend((ROOT / ".toolchain").rglob("*.deb"))
    if gate == "G05":
        artifact_paths.extend(
            ROOT / relative_path
            for relative_path in (
                "arch/aarch64/g05.c",
                "arch/x86_64/g05.c",
                "boot/aarch64/efi_main.c",
                "boot/aarch64/g05_context.S",
                "boot/aarch64/vectors.S",
                "boot/x86_64/efi_main.c",
                "boot/x86_64/exceptions.S",
                "boot/x86_64/g05_context.S",
                "kernel/g05.c",
                "kernel/g05.h",
                "kernel/process.c",
                "kernel/process.h",
                "scripts/build-aarch64.sh",
                "scripts/build-x86_64.sh",
                "scripts/run-qemu-aarch64.sh",
                "scripts/run-qemu-x86_64.sh",
                "scripts/test-process-host.sh",
                "tests/gates/G05_process_test.c",
                "tests/gates/G05_test.py",
            )
            if (ROOT / relative_path).is_file()
        )
    if gate == "G06":
        artifact_paths.extend(
            ROOT / relative_path
            for relative_path in (
                "boot/aarch64/efi_main.c",
                "boot/x86_64/efi_main.c",
                "kernel/storage.c",
                "kernel/storage.h",
                "scripts/build-aarch64.sh",
                "scripts/build-x86_64.sh",
                "scripts/test-storage-host.sh",
                "tests/gates/G06_storage_test.c",
                "tests/gates/G06_test.py",
            )
            if (ROOT / relative_path).is_file()
        )
    if gate == "G07":
        artifact_paths.extend(
            ROOT / relative_path
            for relative_path in (
                "boot/aarch64/efi_main.c",
                "boot/x86_64/efi_main.c",
                "kernel/network.c",
                "kernel/network.h",
                "scripts/build-aarch64.sh",
                "scripts/build-x86_64.sh",
                "scripts/run-qemu-aarch64.sh",
                "scripts/run-qemu-x86_64.sh",
                "scripts/test-network-host.sh",
                "tests/gates/G07_network_test.c",
                "tests/gates/G07_test.py",
            )
            if (ROOT / relative_path).is_file()
        )
    if gate == "G08":
        artifact_paths.extend(
            ROOT / relative_path
            for relative_path in (
                "boot/aarch64/efi_main.c",
                "boot/aarch64/vectors.S",
                "boot/uefi/efi.h",
                "boot/x86_64/efi_main.c",
                "docs/G08_PROGRESS.md",
                "kernel/g04.c",
                "kernel/graphics.c",
                "kernel/graphics.h",
                "scripts/build-aarch64.sh",
                "scripts/build-x86_64.sh",
                "scripts/run-qemu-input.py",
                "scripts/test-graphics-host.sh",
                "tests/gates/G08_graphics_test.c",
                "tests/gates/G08_test.py",
            )
            if (ROOT / relative_path).is_file()
        )
    if gate == "G09":
        artifact_paths.extend(
            ROOT / relative_path
            for relative_path in (
                "arch/aarch64/g05.c",
                "arch/x86_64/g05.c",
                "boot/aarch64/efi_main.c",
                "boot/x86_64/efi_main.c",
                "docs/G09_PROGRESS.md",
                "kernel/abi.h",
                "kernel/app_loader.c",
                "kernel/app_loader.h",
                "kernel/process.h",
                "sdk/examples/hello.c",
                "sdk/include/experimentalos.h",
                "scripts/build-aarch64.sh",
                "scripts/build-x86_64.sh",
                "scripts/test-app-loader-host.sh",
                "tests/gates/G09_app_loader_test.c",
                "tests/gates/G09_test.py",
            )
            if (ROOT / relative_path).is_file()
        )
    if gate == "G10":
        artifact_paths.extend(
            ROOT / relative_path
            for relative_path in (
                "boot/aarch64/efi_main.c",
                "boot/x86_64/efi_main.c",
                "docs/G10_PROGRESS.md",
                "kernel/desktop.c",
                "kernel/desktop.h",
                "kernel/graphics.c",
                "kernel/graphics.h",
                "scripts/build-aarch64.sh",
                "scripts/build-x86_64.sh",
                "scripts/run-qemu-input.py",
                "scripts/test-desktop-host.sh",
                "tests/gates/G10_desktop_test.c",
                "tests/gates/G10_test.py",
            )
            if (ROOT / relative_path).is_file()
        )
    if gate == "G11":
        artifact_paths.extend(
            ROOT / relative_path
            for relative_path in (
                "boot/aarch64/efi_main.c",
                "boot/x86_64/efi_main.c",
                "docs/G11_PROGRESS.md",
                "kernel/desktop.c",
                "kernel/desktop.h",
                "kernel/graphics.c",
                "kernel/graphics.h",
                "scripts/build-aarch64.sh",
                "scripts/build-x86_64.sh",
                "scripts/run-qemu-input.py",
                "scripts/test-flow-host.sh",
                "tests/gates/G10_test.py",
                "tests/gates/G11_flow_test.c",
                "tests/gates/G11_test.py",
            )
            if (ROOT / relative_path).is_file()
        )
    if gate == "G12":
        artifact_paths.extend(
            ROOT / relative_path
            for relative_path in (
                "boot/aarch64/efi_main.c",
                "boot/uefi/efi.h",
                "docs/ARCHITECTURE.md",
                "docs/G12_PROGRESS.md",
                "docs/IMPLEMENTATION_STATUS.md",
                "docs/KNOWN_LIMITATIONS.md",
                "kernel/device_tree.c",
                "kernel/device_tree.h",
                "kernel/device_tree_test.c",
                "kernel/memory.c",
                "kernel/memory.h",
                "kernel/pi5_driver.c",
                "kernel/pi5_driver.h",
                "kernel/pi5_driver_test.c",
                "kernel/pi5_dwc3_core.c",
                "kernel/pi5_dwc3_core.h",
                "kernel/pi5_usb_host.c",
                "kernel/pi5_usb_host.h",
                "kernel/pi5_gem_host.c",
                "kernel/pi5_gem_host.h",
                "tests/gates/G12_dwc3_core_test.c",
                "kernel/pi5_platform.c",
                "kernel/pi5_platform.h",
                "scripts/gates/run_gate.py",
                "scripts/build-aarch64.sh",
                "scripts/build-x86_64.sh",
                "scripts/run-qemu-input.py",
                "scripts/test-pi5-host.sh",
                "tests/gates/G12_device_tree_test.c",
                "tests/gates/G12_test.py",
            )
            if (ROOT / relative_path).is_file()
        )
    if gate == "G13":
        artifact_paths.extend(
            ROOT / relative_path
            for relative_path in (
                "docs/G13_PROGRESS.md",
                "boot/aarch64/efi_main.c",
                "boot/x86_64/efi_main.c",
                "kernel/ai_core.c",
                "kernel/ai_core.h",
                "kernel/ai_selftest.c",
                "kernel/ai_worker.c",
                "kernel/ai_worker.h",
                "scripts/build-aarch64.sh",
                "scripts/build-x86_64.sh",
                "scripts/run-qemu-input.py",
                "scripts/test-ai-core-host.sh",
                "tests/gates/G13_ai_core_test.c",
                "tests/gates/G13_worker_test.c",
                "tests/gates/G13_test.py",
            )
            if (ROOT / relative_path).is_file()
        )
    if gate == "G14":
        artifact_paths.extend(
           ROOT / relative_path
           for relative_path in (
               "docs/G14_PROGRESS.md",
               "kernel/sha256.c", "kernel/sha256.h",
               "kernel/jsonrpc.c", "kernel/jsonrpc.h",
               "kernel/mcp_registry.c", "kernel/mcp_registry.h",
               "kernel/mcp_server.c", "kernel/mcp_server.h",
               "kernel/mcp_client.c", "kernel/mcp_client.h",
               "kernel/mcp_deploy.c", "kernel/mcp_deploy.h",
               "scripts/test-mcp-host.sh",
               "tests/gates/G14_mcp_test.c",
               "tests/gates/G14_mcp_integration_test.c",
               "tests/gates/G14_test.py",
           )
           if (ROOT / relative_path).is_file()
        )
    if gate == "G15":
        artifact_paths.extend(
           ROOT / relative_path
           for relative_path in (
               "docs/G15_PROGRESS.md",
               "kernel/roboforge_adapter.c", "kernel/roboforge_adapter.h",
               "scripts/test-roboforge-host.sh",
               "tests/gates/G15_roboforge_test.c",
               "tests/gates/G15_roboforge_session.c",
               "tests/gates/G15_test.py",
               "tests/gates/fixtures/roboforge/initialize.json",
               "tests/gates/fixtures/roboforge/tools_list.json",
               "tests/gates/fixtures/roboforge/catalog.json",
               "tests/gates/fixtures/roboforge/status_janus.json",
               "tests/gates/fixtures/roboforge/refract_disabled.json",
           )
           if (ROOT / relative_path).is_file()
        )
    if gate == "G16":
        artifact_paths.extend(
           ROOT / relative_path
           for relative_path in (
               "docs/G16_PROGRESS.md",
               "kernel/hmac_sha256.c", "kernel/hmac_sha256.h",
               "kernel/chroma_adapter.c", "kernel/chroma_adapter.h",
               "kernel/compat_registry.c", "kernel/compat_registry.h",
               "scripts/test-compat-host.sh", "scripts/g16_generate_vectors.py",
               "tests/gates/G16_compat_test.c", "tests/gates/G16_test.py",
               "tests/gates/fixtures/chromaneural/vectors.txt",
           )
           if (ROOT / relative_path).is_file()
        )
    if gate == "G17":
        artifact_paths.extend(
            ROOT / relative_path
            for relative_path in (
                "docs/G17_PROGRESS.md",
                "kernel/eosvm.c", "kernel/eosvm.h", "kernel/editor.c", "kernel/editor.h",
                "kernel/runtime_iface.c", "kernel/runtime_iface.h", "kernel/devpkg.c", "kernel/devpkg.h",
                "kernel/mcp_deploy.c", "kernel/mcp_deploy.h",
                "scripts/test-devtools-host.sh", "tools/eospkg.py",
                "sdk/examples/fib.eos", "sdk/examples/hello.eos",
                "tests/gates/G17_dev_test.c", "tests/gates/G17_integration_test.c",
                "tests/gates/G17_crosscheck_test.c", "tests/gates/G17_test.py",
            )
            if (ROOT / relative_path).is_file()
        )
    if gate == "G18":
        artifact_paths.extend(
            ROOT / relative_path
            for relative_path in (
                "docs/G18_PROGRESS.md",
                "kernel/browser_net.c", "kernel/browser_net.h", "kernel/browser_html.c", "kernel/browser_html.h",
                "kernel/browser_js.c", "kernel/browser_js.h", "kernel/browser_image.c", "kernel/browser_image.h",
                "kernel/browser.c", "kernel/browser.h", "kernel/docsvc.c", "kernel/docsvc.h",
                "scripts/test-browser-host.sh",
                "tests/gates/G18_browser_test.c", "tests/gates/G18_test.py",
            )
            if (ROOT / relative_path).is_file()
        )
    if gate == "G19":
        artifact_paths.extend(
            ROOT / relative_path
            for relative_path in (
                "docs/G19_PROGRESS.md",
                "kernel/gfx.c", "kernel/gfx.h", "kernel/gfx_selftest.c",
                "scripts/test-gfx-host.sh", "scripts/run-qemu-input.py",
                "tests/gates/G19_gfx_test.c", "tests/gates/G19_test.py",
            )
            if (ROOT / relative_path).is_file()
        )
    if gate == "G20":
        artifact_paths.extend(
            ROOT / relative_path
            for relative_path in (
                "docs/G20_PROGRESS.md",
                "kernel/chacha20.c", "kernel/chacha20.h", "kernel/secsvc.c", "kernel/secsvc.h",
                "kernel/secdeploy.c", "kernel/secdeploy.h",
                "scripts/test-security-host.sh",
                "tests/gates/G20_security_test.c", "tests/gates/G20_test.py",
            )
            if (ROOT / relative_path).is_file()
        )
    if gate == "G21":
        artifact_paths.extend(
            ROOT / relative_path
            for relative_path in (
                "docs/G21_PROGRESS.md",
                "scripts/test-integration-host.sh", "scripts/build-x86_64.sh", "scripts/build-aarch64.sh",
                "tests/gates/G21_integration_test.c", "tests/gates/G21_test.py",
            )
            if (ROOT / relative_path).is_file()
        )
    if gate == "G22":
        artifact_paths.extend(
            ROOT / relative_path
            for relative_path in (
                "docs/G22_PROGRESS.md", "docs/SECURITY.md", "docs/HARDWARE_VALIDATION_GUIDE.md",
                "docs/REPRODUCIBLE_BUILD.md", "docs/RELEASE_NOTES.md", "docs/KNOWN_LIMITATIONS.md",
                "scripts/build-release.sh", "scripts/test-release-boot.sh",
                "tools/mkimage.py", "tools/checkimage.py", "tools/release_manifest.py",
                "tests/gates/G22_test.py",
                "build/manifests/release-manifest.json", "build/manifests/package-manifest.json",
                "build/manifests/SHA256SUMS",
            )
            if (ROOT / relative_path).is_file()
        )
    artifact_paths = list(dict.fromkeys(artifact_paths))
    for path in artifact_paths:
        if path.is_file():
            report["artifact_hashes"][path.relative_to(ROOT).as_posix()] = sha256(path)
    archive_previous_results(LOG_DIR, gate)
    json_path = LOG_DIR / f"{gate}_RESULT.json"
    md_path = LOG_DIR / f"{gate}_RESULT.md"
    hashes_path = LOG_DIR / f"{gate}_SHA256.txt"
    write_json(json_path, report)
    summary = [
        f"# {gate} test result",
        "",
        f"- Date: {finished}",
        f"- Source commit: `{report['source_commit']}`",
        f"- Platform: {report['platform']}",
        "- Environment: Python gate runner; WSL2/QEMU where required",
        f"- Command: `python {test_path.relative_to(ROOT).as_posix()}`",
        f"- Exit code: `{result.returncode}`",
        f"- Software status: **{status}**",
        f"- Hardware status: **{report['hardware_status']}**",
        f"- Next gate: `{report['next_gate'] or 'none'}`",
        *(
            [f"- Blocked dependent gate: `{report['blocked_next_gate']}` (requires {gate}=SOFTWARE_PASS)"]
            if report["blocked_next_gate"] else []
        ),
        "",
        "## Test output",
        "",
        "```text",
        result.stdout.rstrip(),
        "```",
        "",
        "## Limitations",
        "",
    ]
    summary.extend(f"- {item}" for item in report["known_limitations"])
    if report["remaining_software_work"]:
        summary.extend(["", "## Remaining software work", ""])
        summary.extend(f"- {item}" for item in report["remaining_software_work"])
    if report["hardware_verification"]:
        summary.extend(["", "## Hardware verification", "", report["hardware_verification"]])
    md_path.write_text("\n".join(summary).rstrip() + "\n", encoding="utf-8")
    hash_inputs = tuple(artifact_paths) + (json_path, md_path)
    hashes_path.write_text(
        "".join(
            f"{sha256(path)}  {path.relative_to(ROOT).as_posix()}\n"
            for path in hash_inputs
            if path.is_file()
        ),
        encoding="utf-8",
    )
    print(result.stdout, end="")
    print(f"{gate}: {status}; evidence written under logs/")
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
