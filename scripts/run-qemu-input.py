#!/usr/bin/env python3
"""Boot a test image, inject one USB key through QMP and verify its reports."""

from __future__ import annotations

import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TIMEOUT_SECONDS = int(os.environ.get("EXPERIMENTALOS_QEMU_TIMEOUT", "60"))


def run_checked(command: list[str], env: dict[str, str]) -> None:
    subprocess.run(command, cwd=ROOT, env=env, check=True)


def qmp_command(stream, identifier: int, command: str, arguments=None) -> dict:
    request = {"execute": command, "id": identifier}
    if arguments is not None:
        request["arguments"] = arguments
    stream.write((json.dumps(request) + "\r\n").encode("ascii"))
    stream.flush()
    while True:
        response = json.loads(stream.readline())
        if response.get("id") == identifier:
            if "error" in response:
                raise RuntimeError(f"QMP {command} failed: {response['error']}")
            return response


def wait_for_marker(
    serial_log: Path,
    marker: str,
    process: subprocess.Popen,
    timeout: float,
) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        output = serial_log.read_text(encoding="ascii", errors="replace")
        if marker in output:
            return output
        if process.poll() is not None:
            raise RuntimeError(
                f"QEMU exited with {process.returncode} before {marker!r}.\n{output}"
            )
        time.sleep(0.05)
    output = serial_log.read_text(encoding="ascii", errors="replace")
    raise TimeoutError(f"Timed out waiting for {marker!r}.\n{output}")


def main() -> int:
    if len(sys.argv) not in {2, 3} or sys.argv[1] not in {
        "x86_64", "aarch64"
    } or (len(sys.argv) == 3
          and sys.argv[2] not in {"G04", "G08",           "G10", "G11", "G12", "G13", "G19"}):
        raise SystemExit(
                      "usage: run-qemu-input.py x86_64|aarch64 [G04|G08|G10|G11|G12|G13|G19]")
    architecture = sys.argv[1]
    gate = sys.argv[2] if len(sys.argv) == 3 else "G04"
    env = os.environ.copy()
    env[f"EXPERIMENTALOS_{gate}_TEST"] = "1"

    build_dir = ROOT / "build" / architecture
    serial_log = build_dir / "qemu-serial.log"
    # Gate builds live under build/dev; build/<arch>/esp is the immutable release tree.
    esp_dir = Path(os.environ.get("EXPERIMENTALOS_BUILD_DIR", ROOT / "build" / "dev" / architecture)) / "esp"
    qemu_log = ROOT / "build" / gate.lower() / f"{architecture}-qemu.log"
    qemu_log.parent.mkdir(parents=True, exist_ok=True)

    if architecture == "x86_64":
        build_script = ROOT / "scripts" / "build-x86_64.sh"
        qemu = shutil.which("qemu-system-x86_64")
        if qemu is None:
            raise RuntimeError("qemu-system-x86_64 is unavailable in WSL.")
        ovmf_code = Path("/usr/share/OVMF/OVMF_CODE_4M.fd")
        ovmf_vars_template = Path("/usr/share/OVMF/OVMF_VARS_4M.fd")
        if not ovmf_code.is_file() or not ovmf_vars_template.is_file():
            raise RuntimeError("The required local OVMF firmware is unavailable.")
        vars_path = build_dir / "OVMF_VARS.fd"
        shutil.copy2(ovmf_vars_template, vars_path)
        qemu_command_line = [
            qemu,
            "-machine", "q35", "-m", "256M", "-smp", "1",
            "-no-reboot", "-no-shutdown", "-display", "none",
            "-monitor", "none",
            "-qmp", f"unix:{tempfile.gettempdir()}/osbuild-{gate.lower()}-{os.getpid()}.sock,server=on,wait=off",
            "-serial", f"file:{serial_log}",
            "-device", "qemu-xhci,id=xhci",
            "-device", "usb-kbd,bus=xhci.0",
            "-drive", f"if=pflash,format=raw,readonly=on,file={ovmf_code}",
            "-drive", f"if=pflash,format=raw,file={vars_path}",
            "-drive", f"format=raw,file=fat:rw:{esp_dir}",
        ]
    else:
        if gate == "G12":
            env["EXPERIMENTALOS_QEMU_VIRT"] = "1"
        run_checked(["sh", str(ROOT / "scripts" / "bootstrap-aarch64-emu.sh")], env)
        tool = ROOT / ".toolchain" / "qemu-arm"
        qemu = tool / "root" / "usr" / "bin" / "qemu-system-aarch64"
        firmware = tool / "root" / "usr" / "share" / "AAVMF"
        if not qemu.is_file():
            raise RuntimeError(f"AArch64 QEMU is unavailable: {qemu}")
        env["QEMU_MODULE_DIR"] = str(
            tool / "root" / "usr" / "lib" / "x86_64-linux-gnu" / "qemu"
        )
        env["QEMU_DATADIR"] = str(tool / "root" / "usr" / "share" / "qemu")
        vars_path = build_dir / "AAVMF_VARS.fd"
        shutil.copy2(firmware / "AAVMF_VARS.fd", vars_path)
        build_script = ROOT / "scripts" / "build-aarch64.sh"
        qemu_command_line = [
            str(qemu),
            "-machine", "virt", "-cpu", "cortex-a72", "-m", "1024M",
            "-smp", "1", "-nodefaults", "-no-reboot", "-no-shutdown",
            "-display", "none", "-monitor", "none",
            "-qmp", f"unix:{tempfile.gettempdir()}/osbuild-{gate.lower()}-{os.getpid()}.sock,server=on,wait=off",
            "-serial", f"file:{serial_log}",
            "-device", "qemu-xhci,id=xhci",
            "-device", "usb-kbd,bus=xhci.0",
            "-drive",
            f"if=pflash,format=raw,readonly=on,file={firmware / 'AAVMF_CODE.no-secboot.fd'}",
            "-drive", f"if=pflash,format=raw,file={vars_path}",
            "-drive", f"format=raw,file=fat:rw:{esp_dir}",
        ]
        if gate in {"G08", "G10", "G11"}:
            qemu_command_line.extend(["-device", "ramfb"])

    run_checked(["sh", str(build_script)], env)
    serial_log.write_text("", encoding="ascii")
    qmp_path = Path(tempfile.gettempdir()) / f"osbuild-{gate.lower()}-{os.getpid()}.sock"
    qemu_command_line = [
        item.replace(
            f"unix:{tempfile.gettempdir()}/osbuild-{gate.lower()}-{os.getpid()}.sock",
            f"unix:{qmp_path}",
        )
        for item in qemu_command_line
    ]

    with qemu_log.open("wb") as error_log:
        process = subprocess.Popen(
            qemu_command_line,
            cwd=ROOT,
            env=env,
            stdout=subprocess.DEVNULL,
            stderr=error_log,
        )
        qmp_socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        qmp_file = None
        try:
            deadline = time.monotonic() + TIMEOUT_SECONDS
            while True:
                if process.poll() is not None:
                    raise RuntimeError(
                        f"QEMU exited early with {process.returncode}."
                    )
                try:
                    qmp_socket.connect(str(qmp_path))
                    break
                except (FileNotFoundError, ConnectionRefusedError):
                    if time.monotonic() >= deadline:
                        raise TimeoutError("QEMU QMP socket did not become ready.")
                    time.sleep(0.05)

            qmp_socket.settimeout(3)
            qmp_file = qmp_socket.makefile("rwb")
            greeting = json.loads(qmp_file.readline())
            if "QMP" not in greeting:
                raise RuntimeError("QEMU did not send a QMP greeting.")
            qmp_command(qmp_file, 1, "qmp_capabilities")
            input_marker = f"{gate} INPUT READY"
            wait_for_marker(serial_log, input_marker, process, TIMEOUT_SECONDS)
            input_key = {
                "G04": "a", "G08": "g", "G10": "d", "G11": "f",
                "G12": "p", "G13": "n", "G19": "u",
            }[gate]
            qmp_command(
                qmp_file,
                2,
                "send-key",
                {"keys": [{"type": "qcode", "data": input_key}], "hold-time": 100},
            )
            guest_pass_marker = (
                "G04 KEYBOARD INPUT PASS"
                if gate == "G04"
                else "G08 FRAMEBUFFER WINDOW COMPOSITOR PASS"
                if gate == "G08"
                else "G10 CLASSIC DESKTOP PASS"
                if gate == "G10"
                else "G11 FLOW DESKTOP PASS"
                if gate == "G11"
                else "G13 AI WORKER ISOLATION PASS"
                if gate == "G13"
                else "G19 GFX DAMAGE BUFFER SYNC PASS"
                if gate == "G19"
                else "G12 FDT BOUNDS DISCOVERY PASS"
            )
            output = wait_for_marker(serial_log, guest_pass_marker, process, 20)
            handoff_marker = (
                "G01 UEFI HANDOFF PASS"
                if architecture == "x86_64"
                else "G02 UEFI HANDOFF PASS"
            )
            output = wait_for_marker(serial_log, handoff_marker, process, 20)
            idle_marker = "G01 IDLE" if architecture == "x86_64" else "G02 IDLE"
            wait_for_marker(serial_log, idle_marker, process, 20)
            if gate == "G04":
                checks = (
                    ("G04 DEVICE INFO PASS", "G04-DEVICES", "UEFI console and firmware identity"),
                    ("G04 TIMER PASS", "G04-TIMER", "firmware timer event and monotonic counter"),
                    ("G04 KEYBOARD INPUT PASS", "G04-INPUT", "QMP-injected USB keyboard input"),
                )
            elif gate == "G08":
                checks = (
                    ("G08 FRAMEBUFFER WINDOW COMPOSITOR PASS", "G08-COMPOSITOR", "GOP framebuffer rendered and verified"),
                    ("G08 INPUT EVENTS PASS", "G08-INPUT", "window drag and close input events passed"),
                )
            elif gate == "G10":
                checks = (
                    ("G10 CLASSIC DESKTOP PASS", "G10-DESKTOP", "Classic desktop rendered on the guest GOP framebuffer"),
                    ("G10 LAUNCH INPUT RESIZE CLOSE CLEANUP PASS", "G10-LIFECYCLE", "application launch, resize, close and resource cleanup passed"),
                )
            elif gate == "G11":
                checks = (
                    ("G11 FLOW DESKTOP PASS", "G11-DESKTOP", "Flow desktop rendered on the guest GOP framebuffer"),
                    ("G11 DOCK SWITCH THEME SERVICES PASS", "G11-FLOW-CORE", "dock, app switching, themes and shared app services passed"),
                )
            elif gate == "G13":
                checks = (
                    ("G13 AI WORKER ISOLATION PASS", "G13-WORKER", "process-isolated AI worker, capability grants, memory isolation, crash containment and restart passed"),
                    ("G13 INFERENCE CANCEL CAPABILITY PASS", "G13-INFERENCE", "local inference, incremental delivery, cancellation and capability checks passed"),
                )
            elif gate == "G19":
                checks = (
                    ("G19 GFX DAMAGE BUFFER SYNC PASS", "G19-GFX", "damage tracking, buffer reuse and frame sync matched the full-repaint reference"),
                    ("G19 GPU FALLBACK FAIL-CLOSED PASS", "G19-GPU-FALLBACK", "V3D stub failed closed and the software fallback rendered"),
                )
                for line in output.splitlines():
                    if "G19 MEASURE" in line:
                        print(f"MEASURE {architecture} guest (emulated counter, not Pi 5): {line.split('G19 MEASURE', 1)[1].strip()}")
            else:
                checks = (
                    ("G12 FDT BOUNDS DISCOVERY PASS", "G12-FDT", "bounded firmware device-tree parsing and discovery fixtures passed"),
                    ("G12 HARDWARE DRIVERS NOT VERIFIED", "G12-HARDWARE-LIMIT", "no Raspberry Pi peripheral behavior was claimed"),
                )
            for marker, test_id, description in checks:
                if marker not in output:
                    raise AssertionError(f"Missing guest marker: {marker}")
                print(f"PASS {test_id}: {architecture} guest {description}")
            print(f"PASS {gate}-HANDOFF: {architecture} guest reached firmware handoff and idle")
            print(f"COMMAND: {' '.join(qemu_command_line)}")
            print(f"TOOLCHAIN: {subprocess.check_output([str(qemu), '--version'], text=True, encoding="utf-8").splitlines()[0]}")
            print(f"{gate} QEMU input test complete.")
            return 0
        finally:
            if qmp_file is not None:
                qmp_file.close()
            qmp_socket.close()
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=2)
            if qmp_path.exists():
                qmp_path.unlink()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL QEMU input: {exc}", file=sys.stderr)
        raise SystemExit(1)
