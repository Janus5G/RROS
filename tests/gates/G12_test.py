#!/usr/bin/env python3
"""Validate bounded device-tree discovery and its explicit hardware limits."""

from __future__ import annotations

import os
import shlex
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def check(condition: bool, test_id: str, message: str) -> None:
    if not condition:
        raise AssertionError(f"FAIL {test_id}: {message}")
    print(f"PASS {test_id}: {message}")


def run_command(command: list[str]) -> str:
    print(f"COMMAND: {' '.join(command)}")
    if os.name == "nt":
        root = subprocess.run(
            ["wsl", "--exec", "wslpath", "-a", str(ROOT)],
            text=True, encoding="utf-8", capture_output=True, check=True,
        ).stdout.strip()
        shell_command = "cd " + shlex.quote(root) + " && " + " ".join(
            shlex.quote(part) for part in command
        )
        result = subprocess.run(
            ["wsl", "--exec", "sh", "-lc", shell_command],
            text=True, encoding="utf-8", stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            check=False,
        )
    else:
        result = subprocess.run(
            command, cwd=ROOT, text=True, encoding="utf-8", stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, check=False,
        )
    if result.returncode:
        raise RuntimeError(
            f"Command failed ({result.returncode}):\n{result.stdout}")
    return result.stdout.strip()


def main() -> int:
    host_output = run_command(["sh", "scripts/test-pi5-host.sh"])
    print(host_output)
    check("PASS G12-DEVICE-TREE:" in host_output,
          "G12-HOST", "FDT parsing, UEFI table discovery and capability states pass")
    check("PASS G12-XHCI-DWC3-INTEGRATED-CLOCK-GATE:" in host_output,
          "G12-DWC3-INTEGRATED-CLOCK",
          "DWC3 startup requires integrated USB clock verification even when external dependencies are absent")
    check("PASS G12-XHCI-SUPERSPEED-BULK:" in host_output,
          "G12-XHCI-SUPERSPEED-BULK",
          "USB 3 companion descriptors and bounded non-streaming bulk endpoint contexts pass fixtures")
    check("PASS G12-XHCI-SUPERSPEED-INTERRUPT:" in host_output,
          "G12-XHCI-SUPERSPEED-INTERRUPT",
          "USB 3 periodic companion payloads and interrupt endpoint contexts pass fixtures")
    check("PASS G12-SDIO1-PINCTRL:" in host_output,
          "G12-SDIO1-PINCTRL",
          "BCM2712 C0/D0 pinctrl state is verified read-only with bounded mapping rollback")
    check("PASS G12-BCM2711-AON-L2-IRQ:" in host_output,
          "G12-AON-L2-IRQ",
          "owner-bound, level-triggered HDMI child dispatch remains masked until display prerequisites pass")
    check("PASS G12-BCM2712-HVS-CLOCKS:" in host_output,
          "G12-HVS-CLOCKS",
          "pinned HVS core/display firmware-clock state and rate are verified before HDMI child IRQ enablement")
    check("PASS G12-BCM2712-HVS-RESOURCE-OWNERSHIP:" in host_output,
          "G12-HVS-RESOURCES",
          "HVS MMIO ownership validates the pinned display IRQ and zero-cell IOMMU bindings with rollback and teardown")
    check("PASS G12-DEVICE-TREE-SANITIZERS:" in host_output,
          "G12-SANITIZERS", "FDT parser fixtures pass ASan and UBSan")
    check("PASS G12-AARCH64-MMU-SANITIZERS:" in host_output,
          "G12-MMU-SANITIZERS",
          "bounded AArch64 page-table construction passes ASan and UBSan")
    check("PASS G12-AARCH64-MMU-RUNTIME-SANITIZERS:" in host_output,
          "G12-MMU-RUNTIME-SANITIZERS",
          "guarded MMU installation and MMIO mapping fixtures pass ASan and UBSan")
    check("PASS G12-DWC3-CORE-HOST-INIT:" in host_output
          and "PASS G12-DWC3-CORE-SANITIZERS:" in host_output,
          "G12-DWC3-CORE-HOST-INIT",
          "DWC3 global host-mode init register model passes plain and ASan/UBSan builds")
    check("PASS G12-GEM-HOST-COMPOSITION:" in host_output,
          "G12-GEM-HOST-COMPOSITION",
          "GEM startup composition rolls back at every injected failure and re-brings up (plain and ASan/UBSan)")
    check("PASS G12-USB-HOST-COMPOSITION:" in host_output,
          "G12-USB-HOST-COMPOSITION",
          "USB host startup composition rolls back at every injected MMIO failure (plain and ASan/UBSan)")
    check("PASS G12-RP1-DMA-IRQ:" in host_output
          and "modeled FDT-to-MSI-X/MIP/GIC binding" in host_output
          and "owner/sequence-matched staged transfers" in host_output,
          "G12-RP1-DMA-IRQ",
          "modeled routed DMA dispatch and owner/sequence-gated staged completion pass")
    check("PASS G12-RP1-DMA-ALLOCATOR:" in host_output,
          "G12-RP1-DMA-ALLOCATOR",
          "AArch64 page-pool allocator rejects mismatched cache ownership and host cache callbacks fail closed")

    x86_output = run_command(["sh", "scripts/build-x86_64.sh"])
    print(x86_output)
    check("Built " in x86_output and "BOOTX64.EFI" in x86_output,
          "G12-X86_64", "x86-64 build compiles shared discovery and Pi controller drivers")

    output = run_command([
        "python3", "scripts/run-qemu-input.py", "aarch64", "G12"
    ])
    print(output)
    serial_path = ROOT / "build" / "aarch64" / "qemu-serial.log"
    serial = serial_path.read_text(encoding="ascii", errors="replace")
    check("G12 FDT BOUNDS DISCOVERY PASS" in serial,
          "G12-AARCH64", "AArch64 QEMU guest passed the FDT fixture")
    check("G12 RESOURCE GIC DMA BCM2712 PCIE RP1 MSIX IRQ GPIO BINDING PL011 IRQ I2C SPI FIXTURES PASS" in serial,
          "G12-AARCH64-DRIVERS",
          "AArch64 guest passed FDT resources, GIC, DMA, BCM2712 PCIe/RP1, GPIO, PL011, I2C and SPI fixtures")
    check("G12 AARCH64 MMU TABLE FIXTURE PASS" in serial,
          "G12-AARCH64-MMU-GUEST",
          "AArch64 guest executed isolated page-table-construction fixtures")
    check("G12 AARCH64 MMU INSTALL PASS" in serial,
          "G12-AARCH64-MMU-INSTALL",
          "AArch64 QEMU installed a validated 4-KiB stage-1 translation table")
    check("G12 ACTIVE MMIO MAP PASS" in serial,
          "G12-AARCH64-MMIO-MAP",
          "AArch64 QEMU exercised a non-identity Device mapping and TLB-backed teardown")
    check("G12 AARCH64 DMA CACHE MAINTENANCE PASS" in serial,
          "G12-AARCH64-DMA-CACHE",
          "AArch64 QEMU executed owner-checked cache maintenance on a UEFI-backed page")
    check("G12 AARCH64 DMA POOL TYPE REJECTION PASS" in serial,
          "G12-AARCH64-DMA-POOL-REJECTION",
          "AArch64 QEMU rejected a DMA pool range whose UEFI memory descriptor was not LoaderData")
    check("G12 AARCH64 DMA PRE-EXIT PAGE ALLOCATOR PASS" in serial,
          "G12-AARCH64-DMA-PRE-EXIT-ALLOCATOR",
          "AArch64 QEMU verified the owner-matched DMA allocator before firmware handoff")
    check("G12 AARCH64 DMA RUNTIME ALLOCATOR PASS" in serial,
          "G12-AARCH64-DMA-RUNTIME-ALLOCATOR",
          "AArch64 QEMU allocated, synchronized, and released a UEFI-backed page after ExitBootServices and MMU installation")
    check("G12 GIC START NOT VERIFIED" in serial,
          "G12-AARCH64-GIC-LIMIT",
          "QEMU without firmware FDT did not claim target GIC startup")
    check("G12 HARDWARE DRIVERS NOT VERIFIED" in serial,
          "G12-AARCH64-LIMIT", "guest did not claim unimplemented Pi drivers")
    check("G12 device-tree parser and capability fixtures failed" not in serial,
          "G12-AARCH64-FAILURE", "guest reported no discovery test failure")
    progress = (ROOT / "docs" / "G12_PROGRESS.md").read_text(encoding="utf-8")
    check("## Capability state at G12 acceptance" in progress
          and "Nothing below is operational on physical hardware" in progress,
          "G12-CAPABILITY-LABELS",
          "unfinished drivers are explicitly labeled non-operational")
    print("G12 software acceptance checks complete; hardware remains HARDWARE_NOT_VERIFIED.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL G12: {exc}", file=sys.stderr)
        raise SystemExit(1)
