#!/usr/bin/env python3
"""AArch64 QEMU/UEFI virtual-platform validation harness for ExperimentalOS (RROS).

Boots the release Pi 5 image as a generic AArch64 UEFI guest (QEMU `virt`). This is NOT BCM2712/RP1
hardware verification. Release artifacts are only read; the image is attached through a temporary
qcow2 copy-on-write overlay and UEFI variables live in a temporary private copy.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / ".toolchain" / "qemu-arm" / "root"
QEMU = TOOL / "usr" / "bin" / "qemu-system-aarch64"
FIRMWARE = TOOL / "usr" / "share" / "AAVMF"
RELEASE_IMAGE = ROOT / "build" / "aarch64" / "ExperimentalOS-Pi5.img"
BUILD_ESP = ROOT / "build" / "aarch64" / "esp"
BUILD_APP = BUILD_ESP / "EFI" / "BOOT" / "BOOTAA64.EFI"
MANIFEST = ROOT / "build" / "manifests" / "SHA256SUMS"
BOOT_APP = "EFI/BOOT/BOOTAA64.EFI"
LOG_ROOT = ROOT / "logs" / "pi5_uefi_harness"
QEMU_ENV = {
    "QEMU_MODULE_DIR": str(TOOL / "usr/lib/x86_64-linux-gnu/qemu"),
    "QEMU_DATADIR": str(TOOL / "usr/share/qemu"),
}

# Markers the guest must print on any healthy boot (G02/G03 handoff).
CORE_MARKERS = [
    "G02 AARCH64 UEFI ENTRY",
    "G02 EXCEPTION LEVEL VALIDATED",
    "G02 MMU STATE VALIDATED",
    "G02 EXCEPTION VECTOR PASS",
    "G03 FAULT DIAGNOSTICS PASS",
    "G03 MEMORY OWNERSHIP PASS",
    "G02 MEMORY MAP VALIDATED",
    "G02 UEFI HANDOFF PASS",
    "G02 IDLE",
]
# Markers emitted by the G12 driver layer in the guest (present when the image carries G12 code).
DRIVER_MARKERS = [
    "G12 AARCH64 DMA POOL TYPE REJECTION PASS",
    "G12 AARCH64 DMA CACHE MAINTENANCE PASS",
    "G12 AARCH64 DMA PRE-EXIT PAGE ALLOCATOR PASS",
    "G12 AARCH64 DMA RUNTIME ALLOCATOR PASS",
    "G12 AARCH64 MMU INSTALL PASS",
    "G12 ACTIVE MMIO MAP PASS",
    "G12 AARCH64 MMU TABLE FIXTURE PASS",
    "G12 RESOURCE GIC DMA BCM2712 PCIE RP1 MSIX IRQ GPIO BINDING PL011 IRQ I2C SPI FIXTURES PASS",
    "G12 FDT BOUNDS DISCOVERY PASS",
]
# Fail-closed claims: the guest must state these, i.e. it must not claim unverified hardware.
FAIL_CLOSED_MARKERS = [
    "G12 GIC START NOT VERIFIED",
    "G12 UEFI DEVICE TREE UNAVAILABLE",
    "G12 HARDWARE DRIVERS NOT VERIFIED",
]
FORBIDDEN = ["HARDWARE VERIFIED", "DRIVERS OPERATIONAL", "PANIC", "ASSERT", "UNHANDLED EXCEPTION"]

NOT_TESTABLE = [
    ("BCM2712 PCIe root complex / RP1 bridge", "QEMU virt has a generic ECAM host bridge, not the BCM2712 PCIe2/RP1 register map or RP1 endpoint."),
    ("DWC3 USB host (RP1 USBH0/USBH1) behind RP1", "qemu-xhci is a generic PCI xHCI, not a Synopsys DWC3 core behind RP1; USBH clock IDs have no provider descriptors."),
    ("Cadence GEM Ethernet (RP1 eth) startup", "No GEM or RP1 PHY model in QEMU virt; no production power-feedback check exists."),
    ("BCM2712 HDMI/HVS display", "No HDMI/HVS model; nodes disabled in the pinned DTS; firmware mailbox unavailable."),
    ("RP1 CFE/PiSP camera", "No PiSP back/front-end or CSI-2 model in QEMU."),
    ("RP1 I2S/audio-out and AXI-DMA", "No RP1 audio or AXI-DMA model; base I2S nodes absent from the pinned DTS."),
    ("BCM2712 SDIO2 / WLAN", "No BCM2712 SDHCI or WLAN model; no trusted firmware available."),
    ("Raspberry Pi GIC-400 / timer wiring and firmware FDT", "QEMU virt GIC is generic; Pi 5 interrupt topology and firmware FDT are unavailable (guest reports FDT unavailable)."),
    ("Pi 5 firmware mailbox, boot ROM, EEPROM and UEFI chain", "Pi 5 firmware is not bundled and is not emulated."),
    ("RP1 PL011 UART at Pi 5 MMIO addresses", "QEMU virt exposes its PL011 at a different address; Pi 5 addresses are exercised only by host register fixtures."),
    ("Generic PCIe/USB/virtio device enumeration by the guest", "The guest kernel has no generic ECAM PCIe, xHCI or virtio drivers; devices are attached only to prove the boot is unaffected."),
]


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(command: list[str], log: Path, timeout: int | None = None, env: dict | None = None) -> int:
    with log.open("ab") as out:
        out.write(("$ " + " ".join(str(c) for c in command) + "\n").encode())
        out.flush()
        try:
            code = subprocess.run(command, cwd=ROOT, stdout=out, stderr=subprocess.STDOUT,
                                  timeout=timeout, env=env).returncode
        except subprocess.TimeoutExpired:
            code = 124
        out.write(f"[exit {code}]\n".encode())
    return code


def git(*args: str) -> str:
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True).stdout.strip()


class Report:
    def __init__(self, run_dir: Path):
        self.run_dir = run_dir
        self.results: list[dict] = []

    def add(self, name: str, status: str, detail: str, command: str = "", exit_code=None,
            log: Path | None = None, category: str = "") -> None:
        self.results.append({
            "name": name, "status": status, "detail": detail, "command": command,
            "exit_code": exit_code, "category": category,
            "log": log.relative_to(ROOT).as_posix() if log else ""})
        print(f"{status:12} {name}: {detail}")


def verify_manifest(report: Report) -> dict:
    """Read-only SHA-256 verification of every manifest entry."""
    log = report.run_dir / "manifest-verify.log"
    release_prefixes = ("build/aarch64/ExperimentalOS-Pi5.img", "build/x86_64/ExperimentalOS.iso", "build/sdk/")
    esp_apps = ("build/aarch64/esp/EFI/BOOT/BOOTAA64.EFI", "build/x86_64/esp/EFI/BOOT/BOOTX64.EFI")
    checked = ok = 0
    mismatches: list[str] = []
    missing: list[str] = []
    recorded: dict[str, str] = {}
    with log.open("w") as out:
        for line in MANIFEST.read_text().splitlines():
            if not line.strip():
                continue
            expected, rel = line.split(None, 1)
            rel = rel.strip().lstrip("*")
            recorded[rel] = expected
            path = ROOT / rel
            if not path.exists():
                missing.append(rel)
                out.write(f"MISSING {rel}\n")
                continue
            checked += 1
            actual = sha256(path)
            if actual == expected:
                ok += 1
                out.write(f"OK {rel} {actual}\n")
            else:
                mismatches.append(rel)
                out.write(f"MISMATCH {rel} expected={expected} actual={actual}\n")
    bad = mismatches + missing
    release_bad = [m for m in bad if m.startswith(release_prefixes)]
    esp_bad = [m for m in bad if m in esp_apps]
    other_bad = [m for m in bad if m not in release_bad and m not in esp_bad]
    cmd = f"sha256 of every entry in {MANIFEST.relative_to(ROOT).as_posix()} (read-only)"
    if release_bad:
        report.add("release-artifact-sha256", "FAIL", "mismatch/missing: " + ", ".join(release_bad), cmd, 1, log, "integrity")
    else:
        report.add("release-artifact-sha256", "PASS",
                   "release image, ISO and SDK archives match the manifest", cmd, 0, log, "integrity")
    if esp_bad:
        report.add("release-esp-boot-apps-sha256", "FAIL",
                   "ESP boot applications on disk no longer match the release manifest (overwritten by later gate "
                   "test builds into the same build/*/esp path, not by this harness): " + ", ".join(esp_bad)
                   + ". The release image/ISO content is verified separately below.", cmd, 1, log, "integrity")
    else:
        report.add("release-esp-boot-apps-sha256", "PASS", "ESP boot applications match the manifest", cmd, 0, log, "integrity")
    if other_bad:
        report.add("manifest-other-entries", "FAIL",
                   f"{len(other_bad)} other entries differ: " + ", ".join(other_bad[:5]), cmd, 1, log, "integrity")
    else:
        report.add("manifest-other-entries", "PASS", "all other manifest entries match", cmd, 0, log, "integrity")
    return {"checked": checked, "ok": ok, "mismatches": mismatches, "missing": missing, "recorded": recorded}


def check_image(report: Report, recorded: dict) -> None:
    """Structural check of the image, plus embedded-boot-app == manifest-recorded release app."""
    sys.path.insert(0, str(ROOT / "tools"))
    import checkimage  # read-only structural parser

    log = report.run_dir / "checkimage.log"
    ref_rel = "build/aarch64/esp/EFI/BOOT/BOOTAA64.EFI"
    try:
        disk = RELEASE_IMAGE.read_bytes()
        start, end = checkimage.check_gpt(disk)
        fat = checkimage.Fat(disk[start * 512:(end + 1) * 512])
        embedded = fat.read(BOOT_APP)
    except checkimage.CheckError as exc:
        report.add("checkimage-structure", "FAIL", f"image structure rejected: {exc}", "", 1, log, "integrity")
        return
    embedded_hash = hashlib.sha256(embedded).hexdigest()
    tmp_ref = Path(tempfile.mkdtemp(prefix="pi5-ref-")) / "BOOTAA64.EFI"
    tmp_ref.write_bytes(embedded)
    cmd = [sys.executable, "tools/checkimage.py", "disk", RELEASE_IMAGE.relative_to(ROOT).as_posix(),
           BOOT_APP, "aarch64", str(tmp_ref)]
    code = run(cmd, log)
    shutil.rmtree(tmp_ref.parent, ignore_errors=True)
    report.add("checkimage-structure", "PASS" if code == 0 else "FAIL",
               "GPT (both headers/CRCs), single ESP, FAT volume and AArch64 EFI PE boot application read back "
               "from the image (reference = the embedded application)", " ".join(cmd), code, log, "integrity")
    want = recorded.get(ref_rel)
    ok = want == embedded_hash
    report.add("image-boot-app-matches-release-manifest", "PASS" if ok else "FAIL",
               f"boot application embedded in the release image sha256={embedded_hash}; manifest records {want}",
               "checkimage.Fat.read + sha256", 0 if ok else 1, log, "integrity")


def serial_scan(text: str) -> dict:
    return {
        "core_missing": [m for m in CORE_MARKERS if m not in text],
        "driver_missing": [m for m in DRIVER_MARKERS if m not in text],
        "fail_closed_missing": [m for m in FAIL_CLOSED_MARKERS if m not in text],
        "forbidden_present": [m for m in FORBIDDEN if m in text.upper()],
    }


INPUT_KEYS = {"G04": "a", "G08": "g", "G10": "d", "G11": "f", "G12": "p", "G13": "n", "G19": "u"}


def qmp_exchange(stream, ident: int, command: str, arguments=None) -> dict:
    request = {"execute": command, "id": ident}
    if arguments is not None:
        request["arguments"] = arguments
    stream.write((json.dumps(request) + "\r\n").encode())
    stream.flush()
    while True:
        response = json.loads(stream.readline())
        if response.get("id") == ident:
            if "error" in response:
                raise RuntimeError(f"QMP {command}: {response['error']}")
            return response


def run_with_input(command: list[str], serial: Path, runlog: Path, qmp_path: Path, timeout: int) -> tuple[int, str]:
    """Boot, wait for the guest `Gxx INPUT READY` marker, inject the matching USB key via QMP, wait for idle."""
    import re
    import socket
    import time

    note = ""
    with runlog.open("wb") as err:
        proc = subprocess.Popen(command, cwd=ROOT, env={**os.environ, **QEMU_ENV}, stdout=subprocess.DEVNULL, stderr=err)
        try:
            deadline = time.monotonic() + timeout
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            while True:
                try:
                    sock.connect(str(qmp_path))
                    break
                except (FileNotFoundError, ConnectionRefusedError):
                    if proc.poll() is not None or time.monotonic() > deadline:
                        return 125, "QMP socket never became ready"
                    time.sleep(0.05)
            sock.settimeout(3)
            stream = sock.makefile("rwb")
            json.loads(stream.readline())
            qmp_exchange(stream, 1, "qmp_capabilities")
            sent = False
            reached_idle = False
            while time.monotonic() < deadline:
                text = serial.read_text(errors="replace")
                match = re.search(r"(G\d\d) INPUT READY", text)
                if match and not sent:
                    key = INPUT_KEYS.get(match.group(1))
                    if key is None:
                        return 126, f"no key mapping for {match.group(1)}"
                    qmp_exchange(stream, 2, "send-key",
                                 {"keys": [{"type": "qcode", "data": key}], "hold-time": 100})
                    sent = True
                    note = f"injected '{key}' for {match.group(1)}"
                if sent and ("G02 IDLE" in text or "G02 FAIL" in text):
                    reached_idle = True
                    break
                if proc.poll() is not None:
                    return proc.returncode, note
                time.sleep(0.2)
            if not reached_idle:
                note += " (timed out before the guest reached idle)" if sent else " (guest never printed INPUT READY)"
                return 124, note
            return 0, note
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(5)
                except subprocess.TimeoutExpired:
                    proc.kill()
            err.write(f"\n[harness] {note}\n".encode())


def boot(name: str, report: Report, drive_args: list[str], tmp: Path, description: str,
         cpu: str = "cortex-a72", gic: str = "", smp: int = 1, mem: str = "1024M",
         extra: list[str] | None = None, timeout: int = 40, drivers: bool = True,
         qmp_input: bool = False) -> str:
    serial = report.run_dir / f"{name}.serial.log"
    runlog = report.run_dir / f"{name}.qemu.log"
    serial.write_bytes(b"")
    vars_copy = tmp / f"{name}.AAVMF_VARS.fd"
    shutil.copyfile(FIRMWARE / "AAVMF_VARS.fd", vars_copy)
    qmp_path = tmp / f"{name}.qmp.sock"
    command = [
        "timeout", "--signal=TERM", "--kill-after=2s", f"{timeout}s", str(QEMU),
        "-machine", "virt" + (f",gic-version={gic}" if gic else ""), "-cpu", cpu, "-m", mem, "-smp", str(smp),
        "-nodefaults", "-no-reboot", "-no-shutdown", "-display", "none", "-monitor", "none",
        "-serial", f"file:{serial}",
        "-drive", f"if=pflash,format=raw,readonly=on,file={FIRMWARE / 'AAVMF_CODE.no-secboot.fd'}",
        "-drive", f"if=pflash,format=raw,file={vars_copy}",
        *drive_args, *(extra or []),
    ]
    if qmp_input:
        command[:5] = [str(QEMU)]
        command += ["-qmp", f"unix:{qmp_path},server=on,wait=off"]
        code, note = run_with_input(command, serial, runlog, qmp_path, timeout)
    else:
        code = run(command, runlog, env={**os.environ, **QEMU_ENV})
        note = ""
    text = serial.read_text(errors="replace")
    scan = serial_scan(text)
    problems = []
    if qmp_input and code != 0:
        problems.append(f"input-driven run ended with {code} ({note})")
    elif not qmp_input and code not in (124, 137):
        problems.append(f"QEMU exited {code} (expected timeout 124/137 while the guest idles)")
    if scan["core_missing"]:
        problems.append("missing core markers: " + "; ".join(scan["core_missing"]))
    if drivers and scan["driver_missing"]:
        problems.append("missing driver markers: " + "; ".join(scan["driver_missing"]))
    if drivers and scan["fail_closed_missing"]:
        problems.append("missing fail-closed markers: " + "; ".join(scan["fail_closed_missing"]))
    if scan["forbidden_present"]:
        problems.append("forbidden text: " + ", ".join(scan["forbidden_present"]))
    if "G02 FAIL" in text:
        problems.append("guest reported: " + next(l for l in text.splitlines() if "G02 FAIL" in l))
    cmd_text = " ".join(command) + (f"  # {note}" if note else "")
    if problems:
        report.add(name, "FAIL", description + " | " + " | ".join(problems), cmd_text, code, serial, "boot")
    else:
        report.add(name, "PASS", description, cmd_text, code, serial, "boot")
    return text



def main() -> int:
    global BUILD_ESP, BUILD_APP
    parser = argparse.ArgumentParser()
    parser.add_argument("--host-fixtures", action="store_true",
                        help="also run scripts/test-pi5-host.sh (register-model fixtures, ASan/UBSan)")
    parser.add_argument("--timeout", type=int, default=40)
    parser.add_argument("--driver-esp", type=Path,
                        help="isolated G12 fixture ESP; release ESP remains the manifest reference")
    args = parser.parse_args()
    if args.driver_esp:
        BUILD_ESP = args.driver_esp.resolve()
        BUILD_APP = BUILD_ESP / BOOT_APP

    stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    run_dir = LOG_ROOT / stamp
    run_dir.mkdir(parents=True, exist_ok=True)
    report = Report(run_dir)
    started = utc_now()
    revision = git("rev-parse", "HEAD")
    dirty = [l for l in git("status", "--porcelain", "--", "kernel", "boot", "scripts", "tests", "tools").splitlines()
             if "pi5_uefi_harness" not in l and "run_pi5_gate_test" not in l]
    version = subprocess.run([str(QEMU), "--version"], capture_output=True, text=True,
                             env={**os.environ, **QEMU_ENV}).stdout.splitlines()[:1] if QEMU.exists() else []
    env_info = {"host": sys.platform, "python": sys.version.split()[0], "qemu": version,
                "firmware": "AAVMF_CODE.no-secboot.fd (edk2 toolchain copy)",
                "platform": "QEMU virt, generic AArch64 UEFI guest; NOT Raspberry Pi 5 hardware"}

    for needed in (QEMU, FIRMWARE / "AAVMF_CODE.no-secboot.fd", RELEASE_IMAGE, BUILD_APP, MANIFEST):
        if not needed.exists():
            report.add("prerequisites", "FAIL", f"missing {needed}", "", 1, None, "setup")
            write_report(report, revision, dirty, env_info, started, {})
            return 1

    image_before, app_before = sha256(RELEASE_IMAGE), sha256(BUILD_APP)
    manifest_info = verify_manifest(report)
    check_image(report, manifest_info["recorded"])

    tmp = Path(tempfile.mkdtemp(prefix="pi5-uefi-"))
    texts: dict[str, str] = {}
    try:
        def overlay(tag: str) -> list[str]:
            path = tmp / f"{tag}.qcow2"
            code = run(["qemu-img", "create", "-f", "qcow2", "-F", "raw", "-b", str(RELEASE_IMAGE), str(path)],
                       report.run_dir / f"{tag}.qemu-img.log")
            if code != 0:
                raise RuntimeError(f"qemu-img overlay failed for {tag}")
            return ["-drive", f"format=qcow2,file={path}"]

        base = "release image through a qcow2 overlay"
        t = args.timeout
        texts["release-baseline"] = boot("release-baseline", report, overlay("baseline"), tmp, timeout=t,
                                         drivers=False, description=f"{base}; cortex-a72, default GIC, 1 CPU")
        texts["release-a76-gicv3"] = boot("release-a76-gicv3", report, overlay("a76v3"), tmp, cpu="cortex-a76",
                                          gic="3", timeout=t, drivers=False, description=f"{base}; Pi 5 core model cortex-a76, GICv3")
        texts["release-a76-gicv2"] = boot("release-a76-gicv2", report, overlay("a76v2"), tmp, cpu="cortex-a76",
                                          gic="2", timeout=t, drivers=False, description=f"{base}; cortex-a76, GICv2 (generic QEMU GIC only)")
        texts["release-smp4-512m"] = boot("release-smp4-512m", report, overlay("smp4"), tmp, smp=4, mem="512M",
                                          timeout=t, drivers=False, description=f"{base}; 4 CPUs, 512 MiB (memory-map robustness)")
        disk = tmp / "storage.qcow2"
        run(["qemu-img", "create", "-f", "qcow2", str(disk), "8M"], report.run_dir / "storage.qemu-img.log")
        devices = ["-device", "qemu-xhci,id=xhci", "-device", "usb-kbd,bus=xhci.0",
                   "-netdev", "user,id=n0", "-device", "virtio-net-pci,netdev=n0", "-device", "virtio-rng-pci",
                   "-drive", f"if=none,id=disk1,format=qcow2,file={disk}", "-device", "virtio-blk-pci,drive=disk1"]
        texts["release-pci-usb-storage"] = boot(
            "release-pci-usb-storage", report, overlay("pci"), tmp, extra=devices, timeout=t, drivers=False,
            description=f"{base} plus generic PCIe devices (xHCI+USB keyboard, virtio net/rng/blk): boot unaffected, "
                        "devices are not driven by the guest")
        input_devices = ["-device", "qemu-xhci,id=xhci", "-device", "usb-kbd,bus=xhci.0"]
        esp_drive = ["-drive", f"format=raw,readonly=on,file=fat:ro:{BUILD_ESP}"]
        texts["g12-guest-a72"] = boot(
            "g12-guest-a72", report, esp_drive, tmp, extra=input_devices, timeout=t, qmp_input=True,
            description="G12 driver-test guest (selected fixture ESP, read-only FAT view): QMP-injected USB key "
                        "triggers DMA/MMU/MMIO/resource/FDT driver fixtures inside the guest; cortex-a72")
        texts["g12-guest-a76"] = boot(
            "g12-guest-a76", report, esp_drive, tmp, extra=input_devices, timeout=t, qmp_input=True, cpu="cortex-a76",
            gic="3", description="G12 driver-test guest as above on cortex-a76 / GICv3")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    def stream(text: str) -> list[str]:
        return [l for l in text.splitlines() if l.startswith(("EXPERIMENTALOS:", "G0", "G1"))]
    same = stream(texts["release-baseline"]) == stream(texts["release-a76-gicv3"]) != []
    report.add("guest-marker-determinism", "PASS" if same else "FAIL",
               "marker stream " + ("identical" if same else "differs") + " between cortex-a72 and cortex-a76/GICv3 boots",
               "", 0 if same else 1, None, "boot")

    intact = image_before == sha256(RELEASE_IMAGE) and app_before == sha256(BUILD_APP)
    report.add("artifacts-unmodified", "PASS" if intact else "FAIL",
               "release image and build boot application hashes " + ("unchanged" if intact else "CHANGED") + " after all boots",
               "", 0 if intact else 1, None, "integrity")

    if args.host_fixtures:
        log = report.run_dir / "host-fixtures.log"
        cmd = ["sh", "scripts/test-pi5-host.sh"]
        code = run(cmd, log, timeout=1800)
        report.add("host-register-model-fixtures", "PASS" if code == 0 else "FAIL",
                   "BCM2712/RP1 driver register-model fixtures, plain + ASan/UBSan (HOST_FIXTURE, not hardware)",
                   " ".join(cmd), code, log, "host-fixture")
    else:
        report.add("host-register-model-fixtures", "NOT_RUN", "enable with --host-fixtures", "", None, None, "host-fixture")

    for item, reason in NOT_TESTABLE:
        report.add(item, "NOT_TESTABLE", reason, "", None, None, "hardware")

    write_report(report, revision, dirty, env_info, started, manifest_info)
    return 1 if any(r["status"] == "FAIL" for r in report.results) else 0


def write_report(report: Report, revision: str, dirty: list[str], env_info: dict, started: str,
                 manifest_info: dict) -> None:
    counts: dict[str, int] = {}
    for r in report.results:
        counts[r["status"]] = counts.get(r["status"], 0) + 1
    finished = utc_now()
    (report.run_dir / "report.json").write_text(json.dumps({
        "started": started, "finished": finished, "source_revision": revision,
        "uncommitted_source_changes": dirty, "environment": env_info, "counts": counts,
        "manifest": {k: v for k, v in manifest_info.items() if k != "recorded"}, "results": report.results}, indent=2) + "\n")
    md = ["# Pi 5 AArch64 QEMU/UEFI virtual-platform validation report", "",
          f"- Started: {started}  Finished: {finished}",
          f"- Source revision: `{revision}`" + (" (uncommitted source changes present)" if dirty else ""),
          f"- Environment: {env_info['platform']}; QEMU {env_info['qemu']}; Python {env_info['python']}",
          "- Scope: generic QEMU `virt` guest only. This is NOT BCM2712/RP1 hardware verification.",
          f"- Totals: {counts}", "",
          "| Test | Status | Exit | Detail | Log |", "|---|---|---|---|---|"]
    for r in report.results:
        md.append(f"| {r['name']} | {r['status']} | {r['exit_code']} | {r['detail'].replace('|', '/')} | {r['log']} |")
    md += ["", "## Exact commands", ""]
    md += [f"- `{r['name']}`: `{r['command']}`" for r in report.results if r["command"]]
    md += ["", "## Evidence hashes (SHA-256 of captured logs)", ""]
    md += [f"- `{p.relative_to(ROOT).as_posix()}` {sha256(p)}" for p in sorted(report.run_dir.glob("*.log"))]
    (report.run_dir / "REPORT.md").write_text("\n".join(md) + "\n")
    (LOG_ROOT / "LATEST").write_text(report.run_dir.name + "\n")


if __name__ == "__main__":
    sys.exit(main())
