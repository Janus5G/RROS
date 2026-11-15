# Hardware validation guide

> **BLOCKED for community execution.** The physical steps retained below
> are historical reference, not an approved procedure. The current
> [safety gate](community/SAFETY_GATE.json) overrides them. Use the separate
> [engineering-review guide](community/HARDWARE_GUIDE.md); do not attach
> hardware, write media or power up RROS based on this document.

Status of every item below: HARDWARE_NOT_VERIFIED. No Raspberry Pi 5 hardware has been available; the
procedures are for the owner to run, and results must be recorded as actual observations.

## Raspberry Pi 5 image (`build/aarch64/ExperimentalOS-Pi5.img`)

The image is a GPT disk with one FAT32 EFI System Partition containing `EFI/BOOT/BOOTAA64.EFI`. It was only
structurally checked (`tools/checkimage.py`) and booted as a generic AArch64 UEFI guest in QEMU `virt`.
It is not boot-verified on Pi 5. The Pi 5 does not provide UEFI itself: booting requires a separately
obtained, owner-reviewed UEFI firmware chain for the Pi 5, which is not bundled and not evaluated here.

Procedure (owner-authorized only; this project never flashes):
1. Verify the image: `sha256sum -c` against `build/manifests/SHA256SUMS` and run
   `python3 tools/checkimage.py disk build/aarch64/ExperimentalOS-Pi5.img EFI/BOOT/BOOTAA64.EFI aarch64 build/aarch64/esp/EFI/BOOT/BOOTAA64.EFI`.
2. Use a disposable SD card or USB device; confirm the target device by size and serial before writing.
3. Attach a serial console (UART) before first boot and capture all output.
4. Record observed behavior: firmware handoff, `G02`-style serial markers, display, timer, GIC, RP1 devices.
5. Treat any peripheral (GPIO, UART, I2C, SPI, PCIe, USB, microSD, Ethernet, HDMI) as unverified until
   observed individually; the drivers are fixture-tested only.

## Recovery

- Re-flash a known-good OS image on the SD card; the OS image never modifies firmware or EEPROM.
- Keep the original card. If boot fails, capture serial output and report it; do not edit manifests.

## x86-64 ISO

`build/x86_64/ExperimentalOS.iso` boots as a UEFI guest (OVMF/QEMU, tested). Physical x86-64 boot is not
verified.

## Virtual-platform harness (QEMU, not hardware)

Run scripts\run_pi5_gate_test.ps1 [-HostFixtures] (or sh scripts/run_pi5_gate_test.sh [--host-fixtures]). It verifies manifests read-only, boots the release image through a qcow2 overlay with private UEFI variables on several QEMU configurations, runs the G12 driver-test guest via QMP key injection, and writes logs/pi5_uefi_harness/<UTC stamp>/REPORT.md. Results are PASS/FAIL/NOT_TESTABLE; generic QEMU never verifies BCM2712/RP1 hardware.

## Physical validation package

See [PI5_HARDWARE_VALIDATION_PLAN.md](PI5_HARDWARE_VALIDATION_PLAN.md). The release image is not directly bootable/observable on a Pi 5 (blockers B1-B7). Use the isolated probe package below instead.

## Pi 5 hardware-probe image (isolated; never the release image)

Status vocabulary: SOFTWARE_PASS (host/QEMU only), HARDWARE_NOT_VERIFIED, BLOCKED_BY_PHYSICAL_HARDWARE, FAIL. Nothing below is physical evidence until a captured Pi 5 serial log is classified PI5_PROBE_PASS.

Build and verify (all outputs under `build/pi5-probe*`, `build/dev/`; release artifacts untouched):
These build commands require the full source checkout, not an extracted probe package.

```
sh scripts/test-boot-console-host.sh build/pi5-boot-chain-cache/bcm2712-rpi-5-b.dtb   # plain + ASan/UBSan, real Pi 5 DTB
sh scripts/build-pi5-probe.sh                      # build/pi5-probe/esp/EFI/BOOT/BOOTAA64.EFI
sh scripts/run-pi5-probe-qemu.sh                   # QEMU virt,acpi=off; evaluator mode qemu -> SOFTWARE_PASS
python scripts/pi5_hw/build_probe_package.py       # build/pi5-probe-package (card/, PACKAGE_SHA256SUMS)
```

Pinned firmware (`scripts/pi5_hw/boot_chain_pins.json`): worproject/rpi5-uefi v0.3 (2024-03-16), `RPi5_UEFI_Release_v0.3.zip` SHA-256 `2337df31...732724`. The zip is not bundled; the owner downloads it from the pinned URL into `build/pi5-boot-chain-cache/`, and the package script refuses any hash mismatch. Licence terms (TF-A, edk2-non-osi, Broadcom parts) require owner review. Upstream tested on BCM2712 C1; D0 boards may need a newer EEPROM and risk display problems. Upstream: RP1 Ethernet and EEPROM NVRAM do not work.

Card layout (FAT32, owner-authorized write with `write_image_safe.ps1` safeguards or manual copy): contents of `build/pi5-probe-package/card/` at the root: `config.txt`, `RPI_EFI.fd`, `bcm2712-rpi-5-b.dtb`, `EFI/BOOT/BOOTAA64.EFI` (probe). In UEFI setup select Device Manager > Raspberry Pi Configuration > ACPI / Device Tree > System Table Mode = Device Tree (default ACPI gives the probe no FDT; it then reports FAIL for the DT checks, which is the correct fail-closed result).

UART: dedicated Pi 5 debug connector, 115200 8N1, 3.3 V logic. Connector pinout/voltage is NOT verified by this project; owner must confirm against official documentation before wiring. Capture: `python tests/pi5_hw_validation/capture_serial.py --port <port> --out <log>`, then `python tests/pi5_hw_validation/evaluate_probe_log.py --mode pi5 <log>` (exit 0 = PI5_PROBE_PASS, 1 = FAIL, 2 = BLOCKED_BY_PHYSICAL_HARDWARE / no log).

Here `--out` is a directory, not a filename; use the printed `.serial.log` filename for evaluation.
Capture labels must be 1-64 ASCII letters/digits/hyphens/underscores, beginning with a letter or digit.
The capture sends no payload; serial-port opening can nevertheless change adapter control lines.

From an extracted probe-package root, run `python scripts/pi5_hw/build_probe_package.py --verify`,
`python tests/pi5_hw_validation/selftest.py` (requires pyserial 3.5), and
`python tests/pi5_hw_validation/evaluate_probe_log.py --mode qemu qemu_probe_serial.log`.
These are software checks only. The copied builder defaults to read-only standalone verification.
The package includes both evaluators and the self-test: `evaluate_probe_log.py` is for probe output;
`evaluate_serial_log.py` is only for historical G02/G12 output.

Expected serial markers (prefix `PI5PROBE`, lines may appear twice: ConOut and direct UART): `START`, handoff/config-table lines, `CPU el=...`, exception test, memory-map summary, `DT blob_found=1 ... parse=1 nodes=323 root_compatible=raspberrypi,5`, `UART resolved=1 ... base=0x000000107d001000`, `UART-ENABLE about-to-write...`, `UART-ACTIVE source=fdt-pl011`, GIC (arm,gic-400, DT only), timer, `PCIE ... enabled=1 compatible=brcm,bcm2712-pcie`, `PCIE-SUMMARY enabled_controllers>=1 rp1_nodes>=1`, final `PI5PROBE END pass=N fail=0`. The probe never exits boot services, never switches the MMU, and performs no GIC/PCIe MMIO; those remain HARDWARE_NOT_VERIFIED.

Failure and recovery: if output stops after `UART-ENABLE about-to-write`, the firmware UART path faulted; record it as FAIL with the last marker and rely on ConOut/HDMI output. No output at all: check wiring/connector/console setting first (not a probe verdict). To recover, power off, reinsert a card with only the firmware files, or use the official Raspberry Pi imager/rpiboot recovery; the probe changes nothing persistent (no EEPROM, no disk writes). Never run the probe card with an EEPROM change without owner authorization.
