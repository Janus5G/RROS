# Known limitations

- Current RROS Classic and Flow shells
  test UI and process lifecycle fixtures, not independently executing native
  applications or production desktop services.
- G03's page allocator is bounded to a firmware-backed arena; virtual ranges
  are ownership/W^X metadata and do not yet install hardware page-table entries.
- G04 exercises UEFI timer events, counters and firmware console input. It does
  not yet provide native interrupt-controller setup, timer IRQ handling or USB
  device drivers.
- G05 verifies bounded cooperative process switching, isolation faults and
  lifecycle in x86-64/AArch64 QEMU guests. Timer preemption, multicore
  scheduling, a general executable loader and Raspberry Pi 5 hardware remain
  unverified.
- G06 tests a RAM-backed filesystem in guests and a disposable file-backed
  virtual volume on the host. Native physical/firmware block-device drivers,
  production media support and power-failure behavior on physical hardware are
  not verified.
- The G00 audit and local toolchain proof are not a bootable operating system.
- QEMU `virt` validates generic AArch64 firmware behavior only; it does not
  emulate Raspberry Pi 5 BCM2712/RP1 hardware. G02 software acceptance does
  not qualify Raspberry Pi 5 hardware support.
- G12 is recorded SOFTWARE_PASS at fixture level only (conditional; see FINAL_GATE_VERIFICATION_REPORT.md). Register-level RP1 GPIO, PL011 UART, DesignWare
  I2C and SPI code passes host-model fixtures and AArch64 guest fixtures; FDT
  tests also cover compatible lists, PCI assigned-address translation,
  interrupt/clock/reset phandles, bounded DMA ranges and a model-only
  ECAM/RP1-ID scan. These fixtures do not verify
  physical devices. The BCM2712 PCIe host, BAR/resource and MSI-X integration
  is not implemented. GICv2 SPI registration/dispatch and an AArch64 IRQ vector
  entry are fixture tested, but platform startup does not map or activate the
  Pi GIC. Firmware-controlled clock/reset operations remain unimplemented.
  Mapper and DMA allocator callbacks are not wired to page-table permissions,
  the kernel physical allocator or cache maintenance. USB, microSD, HDMI,
  Ethernet, Wi-Fi, audio and camera interfaces remain unimplemented.
- No Raspberry Pi 5 hardware is available; no Pi boot/peripheral behavior is
  verified.
- A local release candidate (0.1.0-rc1) with an x86-64 ISO and a Pi 5 GPT/FAT32 image is built (G22). The Pi 5
  image is structurally checked and booted only as a generic AArch64 UEFI guest in QEMU; it is not boot-verified on
  hardware, not flashed and not approved for publication. No kernel-debug.elf and no Development & Deployment
  Manager executable exist (Not implemented).
- Assessed project-owned source is GPL-3.0-or-later. Third-party fixture,
  firmware and historical reference-document redistribution rights remain
  separately limited; see LICENSE_INVENTORY.md.
- Community physical procedures are blocked by docs/community/SAFETY_GATE.json.
  The ordinary AArch64 EFI build is not an approved physical Pi boot image.
- RoboForge was inspected read-only. No MCP connection or integration test was
  run. Its handover says v0.4.0 while inspected package metadata reports 0.4.1.
- WOS features, screenshots and claims in the reference are not RROS
  features or test evidence.
