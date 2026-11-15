# Raspberry Pi 5 independent hardware validation

[Dansk](RASPBERRY_PI5_TEST_GUIDE.da.md) · [Matrix](HARDWARE_COMPATIBILITY_MATRIX.md)

> **Current community safety gate: BLOCKED.** The physical procedures retained
> below are historical reference, not approved instructions. Do not execute
> them while the [community safety gate](docs/community/SAFETY_GATE.json) has
> unresolved critical requirements. Use the
> [engineering-review guide](docs/community/HARDWARE_GUIDE.md) and offline tools.

## 1. Safety and scope

**HARDWARE_NOT_VERIFIED.** This is not a production installer or a verified
Pi operating system. Use expendable removable media, backups, an adequate Pi 5
PSU and cooling. Disconnect valuable storage and all actuators/PLCs.
Formatting and raw writes destroy data. No command here grants permission to
change EEPROM, purchase equipment, flash firmware or energize hardware.
Those are explicit decisions by the tester/device owner.

The **Pi5-probe archive** was proposed for initial physical diagnostics, but
its execution is now blocked pending safety review. The complete OS
development image still contains a QEMU-specific
UART path and lacks the Pi firmware boot chain. The probe is a separate EFI
diagnostic program; it reports firmware state, FDT, UART, exception, memory and
timer checks. It does not establish functioning native peripheral drivers.

## 2. Record a known-working baseline

Use an existing stock Raspberry Pi OS card first. Save these observations:

```sh
cat /proc/cpuinfo
vcgencmd bootloader_version
vcgencmd version
uname -a
lspci -nn
lsusb -t
ip link
dmesg
```

Record exact board revision, C1/D0 stepping if known, 4/8 GB RAM, PSU,
cooler, cables, cards, USB devices, PCIe HAT and peripherals. Record bootloader
and media layout, not just "latest firmware". Save the device tree if your
baseline has `dtc`: `dtc -I fs -O dtb -o baseline.dtb /proc/device-tree`.
Baseline success is **not** RROS success.

## 3. Verify the release and firmware

Download only from the approved release repository, then verify `SHA256SUMS`
before extracting the probe archive. Record release tag, source commit and both
archive and `card/EFI/BOOT/BOOTAA64.EFI` SHA-256 values.

From the extracted probe-package root, run these read-only software checks:

```powershell
python scripts\pi5_hw\build_probe_package.py --verify
python tests\pi5_hw_validation\selftest.py
python tests\pi5_hw_validation\evaluate_probe_log.py --mode qemu qemu_probe_serial.log
```

The copied builder verifies the standalone package; rebuilding requires the
full source checkout. Install pyserial 3.5 in an isolated environment for the
self-test. `evaluate_probe_log.py` classifies probe logs; the included
`evaluate_serial_log.py` is only for historical G02/G12 logs.

The probe deliberately does **not** bundle third-party firmware. The pinned
dependency is [worproject/rpi5-uefi v0.3](https://github.com/worproject/rpi5-uefi/releases/tag/v0.3).
Its archive is `RPi5_UEFI_Release_v0.3.zip` with SHA-256:

```text
2337df318845448ebcc8a6a587d15dccec44b54c665ece21250d2f6170732724
```

[Pins and per-file hashes](scripts/pi5_hw/boot_chain_pins.json) cover
`config.txt`, `bcm2712-rpi-5-b.dtb` and `RPI_EFI.fd`. Read upstream terms and
compatibility notes before downloading/using it. This archived firmware is
reported upstream against early BCM2712 C1 boards. D0 boards may require newer
EEPROM and have a firmware display risk. Upstream UEFI device claims are not
RROS claims; RP1 Ethernet and EEPROM NVRAM limitations remain relevant.
Do not upgrade EEPROM blindly to address a test failure.

## 4. Prepare only disposable media

1. Independently prepare an expendable FAT32 firmware card. Check its identity
   and back up any data **before** formatting.
2. Extract the hash-verified firmware files to the FAT32 root, retaining their
   names. Copy `card/EFI/BOOT/BOOTAA64.EFI` from the probe archive to the same
   card's `EFI/BOOT/BOOTAA64.EFI`. This is a diagnostic-card layout, not writing
   the legacy disk image.
3. Recheck the copied file hashes and eject safely.
4. Alternatively use firmware SD plus a separate disposable FAT32 USB probe
   medium if the firmware can enumerate it; record the exact arrangement.

No raw writer is needed for this file-copy procedure. The
[Windows writer](scripts/pi5_hw/write_image_safe.ps1) is available for
explicitly chosen image-writing procedures only. Use explicit `-Image` and
`-ExpectedSha256`; its defaults refer to historical artifacts. A
`-TargetFile <regular-file> -Execute` rehearsal touches no disk. The real
raw-disk path remains **unexecuted/unverified**. Never paste a disk-number or
`dd` command from someone else's report.

## 5. UART capture and firmware boot

- Confirm the dedicated Pi 5 debug connector's pinout and **3.3 V logic**
  against [official Raspberry Pi documentation](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html).
  This project's earlier source retrieval did not verify that pinout.
  Never connect 5 V or an adapter power lead; do not guess connector orientation.
- Use Raspberry Pi Debug Probe or a suitable documented USB-UART adapter.
  Common ground; RX/TX crossed as required by the verified pinout.
- Capture at **115200, 8N1, no flow control**, beginning before power-on:

```powershell
python tests\pi5_hw_validation\capture_serial.py --list
python tests\pi5_hw_validation\capture_serial.py --port COM5 --seconds 180 --label pi5-probe
```

Install pyserial 3.5 in your own isolated environment if absent. Linux uses the
actual `/dev/ttyUSB*` or `/dev/ttyACM*` path instead of `COM5`. Capture tool
logs and SHA-256 sidecars are under `logs/pi5_hardware`; it sends no payload
bytes, but opening serial ports can alter adapter control lines.

In Pi UEFI setup select **Device Manager → Raspberry Pi Configuration →
ACPI / Device Tree → System Table Mode → Device Tree**. Default ACPI mode will
not supply the FDT required by the probe. Record all settings and firmware
version. `Esc` enters setup; if needed use the UEFI shell, `map -r`, find the
actual `fsX:` containing `EFI\BOOT\BOOTAA64.EFI` and start that file explicitly.
The shell/firmware appearing is only a firmware observation.

## 6. Classify the probe, not the entire board

```powershell
python tests\pi5_hw_validation\evaluate_probe_log.py --mode pi5 logs\pi5_hardware\YOUR_CAPTURE.serial.log
```

Record `PI5PROBE BEGIN`, revision, root compatible, every `PI5PROBE CHECK`,
resolved UART address, FDT controller descriptions and `PI5PROBE END`.
`PI5_PROBE_PASS` means supplied log markers passed the parser, not independent
authentication, peripheral functionality or overall HARDWARE_PASS.
Missing/empty physical logs are BLOCKED_BY_PHYSICAL_HARDWARE. Keep the full
log even when the parser fails. Do not use `--mode qemu` for a physical claim.

The probe evaluator reconciles duplicate checks and END counters/result,
requires a Pi 5 root-compatible identity and the exact numeric debug UART
address, and rejects conflicting required checks. Short/padded and upper/lower
hexadecimal formatting are accepted. The two Pi-only discovery checks are
informational only in generic QEMU mode. Synthetic logs can pass a parser,
including the legacy evaluator; this is not authentic physical evidence.
Inspect all checks manually and retain the entire original capture.

For PCIe/RP1, FDT-described controller/bridge presence is only discovery
metadata. A physical link, BAR mapping, MSI-X delivery, USB enumeration or
Ethernet traffic requires separate implemented native tests and observations.

## 7. Individual subsystem observations

| Subsystem | Stock-OS baseline to capture | Current RROS validation |
|---|---|---|
| USB | `lsusb -t`, keyboard/mouse and disposable stick, `dmesg` | Controller/clock handoff incomplete; NOT_TESTABLE for native operation |
| Ethernet | `ip link`, `ethtool <actual-interface>`, link/PHY/DHCP | Production GEM startup incomplete; firmware Ethernet limitation; NOT_TESTABLE |
| Display | `kmsprint` or installed `modetest`, EDID, monitor/mode/cable | Firmware picture is not RROS modesetting; native HDMI path NOT_TESTABLE |
| Camera | `rpicam-hello --list-cameras`, module and Pi 5 cable | No capture pipeline; NOT_TESTABLE |
| Audio | `aplay -l`, exact external DAC/overlay/output path | No production I2S/DMA audio; no Pi 5 analog jack; NOT_TESTABLE |
| Wi-Fi/Bluetooth | `iw dev`, `bluetoothctl show`, firmware names/hashes | No trusted bundled firmware/operational stack; NOT_TESTABLE |
| Storage | Media identity, mount layout, read-only baseline files | No verified native Pi storage; do not stress/write valuable media |
| GPIO/I2C/SPI | Device/cable/voltage documentation and DT | Fixtures only; do not attach actuators or drive unknown pins |

Optional baseline tools may be absent; report that fact instead of inventing
output. Do not enable firmware overlays, generate test traffic or drive GPIO
without understanding and authorizing the side effects. Any future RROS-side
PASS needs an exact test command, driver diagnostics and physical observation.
Report stability duration and temperatures as raw observations, not benchmarks
or product guarantees.

## 8. Troubleshooting, recovery and reporting

No output: check power, safe wiring, adapter, baud, capture-before-boot,
FAT32 files and Device Tree mode. Partial output: preserve the last marker,
all FAIL checks and END counters. Firmware cannot locate app: verify media
mapping and the EFI fallback path. D0/display trouble: consult upstream
limitations; do not assume a black screen means a kernel fault.

Power off, remove the test media and restore the known-good baseline card.
EEPROM recovery/update is a separate owner-authorized vendor procedure.
Submit the [structured report](COMMUNITY_TEST_REPORT_TEMPLATE.md) via the
[issue form](.github/ISSUE_TEMPLATE/pi5-hardware.yml): exact hardware,
firmware/settings, release/SHA-256, each result, UART/hash, diagnostics, and
optional genuine photos/video links. Clearly label baseline vs firmware vs
probe vs native RROS results. Redact sensitive data before public upload.
