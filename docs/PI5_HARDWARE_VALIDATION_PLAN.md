# Raspberry Pi 5 hardware validation plan (first physical bring-up)

Status: PACKAGE READY FOR OWNER REVIEW. No physical testing has been authorized or performed. Nothing here is a hardware pass.
Evidence base: QEMU/UEFI harness run `20261113T104652Z` (`logs/pi5_uefi_harness/20261113T104652Z/REPORT.md`) and `docs/HARDWARE_VALIDATION_GUIDE.md`.
QEMU generic virt emulation says nothing about BCM2712/RP1.

## 0. Read this first: the release image is NOT directly bootable/observable on a Pi 5

`build/aarch64/ExperimentalOS-Pi5.img` is a GPT disk with one FAT32 ESP holding only `EFI/BOOT/BOOTAA64.EFI`. Verified gaps:

| # | Missing / blocking item | Kind |
|---|---|---|
| B1 | `boot/aarch64/efi_main.c` writes every RROS marker to the QEMU virt PL011 at `0x09000000`. That is not the Pi 5 debug UART, so RROS output on a Pi 5 will be silent and may fault. Silence proves nothing. | Software, needs a Pi-aware console (separate, owner-authorized change) |
| B2 | No Pi 5 boot firmware in the package: no TF-A/BL31, no EDK2 Pi 5 UEFI (`RPI_EFI.fd` or equivalent), no `config.txt`, no `bcm2712-rpi-5-b` dtb/overlays. A bare ESP is not enough: the Pi 5 EEPROM bootloader loads firmware files, not an EFI app directly. | Missing component |
| B3 | Only available UEFI firmware is `worproject/rpi5-uefi`, which is ARCHIVED/unmaintained, tested on early BCM2712C1; D0 boards may need newer EEPROM, reported to break UEFI graphical output. Owner must review and pin a release and its SHA-256. It is not bundled. | External dependency |
| B4 | No Pi-specific hardware-probe build. The G12 test build checks fixtures only and waits for a QMP-injected key. | Software |
| B5 | G12 drivers are incomplete (see section 8). RROS-side USB/Ethernet/HDMI/camera/audio/wireless tests cannot succeed yet. | Software |
| B6 | ESP/manifest drift (section 1). | Build hygiene |
| B7 | Debug-connector pinout/voltage not verified from an official source (docs returned HTTP 403). | Owner verify |

## 1. Package contents and the ESP/manifest drift

Build the package (read-only on release artifacts, writes only `build/pi5-hw-validation/`):

    python scripts\pi5_hw\build_package.py

Outputs: `release-snapshot/` (byte-identical image copy and the boot app extracted from it), `DRIFT_REPORT.md`, `drift_evidence.json`, `package/`, `PACKAGE_SHA256SUMS`.

Drift finding: `build/aarch64/esp` is one shared output path. Gate runs (the G12 gate with `EXPERIMENTALOS_G12_TEST=1`) overwrite it with a test build; the release image and the boot app embedded in it still match `build/manifests/SHA256SUMS`. Rules:
- Use only `release-snapshot/ExperimentalOS-Pi5.img`.
- Never copy `build/aarch64/esp` to media.
- Never run `scripts/build-release.sh` (its first step deletes `build/aarch64`, including the release image).
- Release manifest records `source_tree_dirty: true`: not exactly reproducible. Durable fix (private release output dir) needs owner authorization.

## 2. Bill of materials

| Item | Notes |
|---|---|
| Raspberry Pi 5 (4 or 8 GB) | Record board revision and SoC stepping (C1/D0) from vendor OS |
| Official 27 W USB-C PD PSU (5.1 V/5 A) | Underpowered supplies cause USB/boot instability |
| Active cooler | |
| microSD card A1/A2, 16-32 GB (firmware media) plus a second card or USB stick for the RROS ESP (>= 80 MiB) | Two-media approach is primary |
| 3.3 V USB-UART adapter (CP2102/FT232 class, 3.3 V only) or Raspberry Pi Debug Probe, with 3-pin JST-SH debug cable | Pinout/voltage: OWNER VERIFY against official Pi 5 documentation |
| micro-HDMI to HDMI cable and a monitor | |
| Wired USB keyboard and a USB flash drive | |
| Ethernet cable and DHCP router/switch | |
| Camera module + 22-pin 0.5 mm CSI cable (Pi 5 connector; standard modules need a 22-to-15-pin adapter cable) | |
| I2S DAC/codec board for the GPIO header (Pi 5 has no 3.5 mm jack) | Only usable once an I2S device tree/driver path exists |
| Multimeter, card reader, ESD care | |

## 3. Boot chain and firmware requirements

1. Baseline (step P0, section 6): boot stock Raspberry Pi OS and record `vcgencmd bootloader_version`, `/proc/cpuinfo` (model, revision), `dmesg`, `/proc/device-tree` dump. This is the only source of ground-truth for the real board.
2. Update EEPROM only with owner approval via the official tool; record before/after versions. Note the D0 / UEFI display caveat (B3).
3. Firmware (owner-reviewed, not bundled): obtain a pinned `rpi5-uefi` release, record its SHA-256, extract to the root of a FAT32 firmware card without renaming files. UEFI behaviour per its docs: Esc = setup, F1 = UEFI shell, otherwise boot an app from storage. Its UART is the dedicated PL011 at 115200 8N1.
4. Primary media layout: firmware card (SD) + RROS ESP on USB (written from the release snapshot). Secondary: single card with firmware files and the RROS ESP merged (more error prone; not recommended first).
5. The firmware must be able to find `EFI/BOOT/BOOTAA64.EFI` on the RROS media; verify in the UEFI shell (`map -r`, `ls fsX:\EFI\BOOT`).

## 4. Safe media preparation

1. `python scripts\pi5_hw\build_package.py` then verify `PACKAGE_SHA256SUMS` hashes (`Get-FileHash`).
2. Image SHA-256 must be `859b02a7599049d5e8a4faa45782fe9bebcc234bd44b834a6cb3238ad5eef2d7` (manifest value).
3. List disks: `powershell -File scripts\pi5_hw\write_image_safe.ps1` (dry run, writes nothing). It marks only USB/SD, non-system, non-boot disks as ELIGIBLE and verifies the hash first.
4. Show a specific target: `... -DiskNumber N`; write: `... -DiskNumber N -Execute` from an elevated shell; you must type the exact displayed phrase (disk number, model, size). It reads back and re-verifies the SHA-256.
5. The real-disk write path is UNEXECUTED/UNTESTED; only the `-TargetFile` rehearsal was exercised. Dry-run first and double-check the disk number against Disk Management.
6. Firmware card contents are copied by hand from the pinned, hash-checked release (not scripted here).

## 5. UART serial

- Settings: 115200 8N1, no flow control. 3.3 V logic only; never connect 5 V.
- Wiring: adapter GND to GND, adapter RX to Pi TX, adapter TX to Pi RX (cross). Pi 5 dedicated debug connector pinout: OWNER VERIFY (not verified here).
- Capture: `python tests\pi5_hw_validation\capture_serial.py --list`, then `--port COMx --label f1-firmware --seconds 180`. Logs land in `logs/pi5_hardware/` with a `.sha256` sidecar. The tool never transmits.
- Classify: `python tests\pi5_hw_validation\evaluate_serial_log.py <log>`. Stages S1 firmware, S2 RROS handoff markers, S3 fail-closed marker; exit 0 only if all PASS.
- Self-test the tooling offline: `python tests\pi5_hw_validation\selftest.py`.

## 6. First-boot acceptance tests

| ID | Test | Expected | Failure criteria / recovery |
|---|---|---|---|
| P0 | Stock Pi OS baseline (executable now) | Boots; model, revision, EEPROM version, dmesg, DT recorded | Fails: PSU, card, cable; stop and fix before anything else |
| F1 | Firmware alive on UART | S1 PASS: firmware text on the debug UART | None: check wiring cross, 3.3 V, baud, firmware card files; reflash card |
| F2 | Firmware loads EFI app | UEFI shell/Boot menu shows the RROS ESP and `BOOTAA64.EFI` | Not listed: wrong media/partition table/firmware version; try shell `map -r` |
| F3 | RROS markers (BLOCKED by B1) | S2: G02/G03 markers on the UART | Expected silent until B1 is fixed. Silence is NOT FAIL of firmware and NOT PASS of RROS; record NOT_OBSERVED |
| F4 | MMU/GIC/timer/device discovery (BLOCKED by B1, B4) | Needs a Pi-aware probe build | Not executable |

Recovery for any boot problem: power off, remove the RROS media, boot the firmware card to the UEFI shell; if bricked, re-write the firmware card; as last resort restore the stock Pi OS card and EEPROM with the official imager. No RROS step writes to EEPROM or other persistent board storage.

## 7. Ordered subsystem tests

Order: USB, Ethernet, HDMI, camera, audio, wireless. Each has an executable-now part (vendor OS observations to build ground truth) and an RROS part (blocked).

| Order | Subsystem | Executable now (stock Pi OS) | RROS-side status | Evidence to keep |
|---|---|---|---|---|
| 1 | USB (RP1 xHCI/DWC3) | `lsusb -t`, `dmesg`, DT nodes for the RP1 USB controllers | BLOCKED: USBH clock IDs 25-28 have no provider in pinned DTS; xHCI handoff incomplete | dmesg, lsusb, DT dump, serial log |
| 2 | Ethernet (RP1 GEM) | `ip link`, `ethtool`, PHY id, link/speed, DHCP lease | BLOCKED: no power-feedback check; GEM composition fixture-verified only; archived UEFI reports RP1 Ethernet not working | ethtool output, dmesg, serial log |
| 3 | HDMI | `modetest`/`kmsprint`, EDID dump (`/sys/class/drm`) | BLOCKED: HDMI/HVS nodes disabled in pinned DTS; no display driver path | EDID, photo of output, serial log |
| 4 | Camera (CFE) | `rpicam-hello --list-cameras`, CSI lane config, dmesg | BLOCKED: identity read only; no capture path | rpicam output, dmesg |
| 5 | Audio (I2S/DMA) | Observe overlay used by the DAC and ALSA `aplay -l` | BLOCKED: base I2S nodes absent in DTS; needs external DAC and driver path | overlay name, aplay output |
| 6 | Wireless (CYW43455 over SDIO) | `iw dev`, `dmesg` firmware load | BLOCKED: no trusted firmware; none is bundled | dmesg, firmware file names/hashes |

Pass criterion for the RROS part of any of these: a recorded serial log showing the driver's own markers plus a physical observation. Until the drivers exist, the correct recorded result is `BLOCKED (reason)`, never PASS. Use `tests/pi5_hw_validation/RESULTS_TEMPLATE.md` to record every result.

## 7b. Blocker status update (probe preparation)

| ID | Status |
|---|---|
| B1 | Resolved in software: `kernel/boot_console.c` resolves the PL011 from firmware FDT `/chosen/stdout-path` (no speculative MMIO; disabled if unresolvable); QEMU path preserved. Host tests incl. real Pi 5 DTB: SOFTWARE_PASS. Physical output: HARDWARE_NOT_VERIFIED |
| B2/B3 | Firmware pinned (`scripts/pi5_hw/boot_chain_pins.json`, rpi5-uefi v0.3, SHA-256 verified). Not bundled, unverified on hardware, licence review and D0 risk remain with the owner. Device Tree mode must be selected |
| B4 | Probe built (`scripts/build-pi5-probe.sh`), QEMU SOFTWARE_PASS only; physical run BLOCKED_BY_PHYSICAL_HARDWARE |
| B6 | Fixed for future builds: dev/gate builds write `build/dev/<arch>`; release path needs `EXPERIMENTALOS_RELEASE_BUILD=1`. The already drifted `build/*/esp` files are left as-is (owner decision) |
| B5, B7 | Unchanged |

See the "Pi 5 hardware-probe image" section of HARDWARE_VALIDATION_GUIDE.md for commands.

## 8. Genuine blockers (concise)

1. B1: hardcoded QEMU UART address in the boot app; RROS unobservable on a Pi 5.
2. B2/B3: no Pi 5 boot firmware/config in the release; only archived third-party UEFI, unpinned, with D0 display risk.
3. B4: no Pi-specific hardware-probe build.
4. B5: unfinished drivers (USB clocks, GEM power feedback, HDMI/HVS disabled nodes, I2S nodes absent, wireless firmware, camera capture).
5. B6: ESP/manifest drift needs an owner-authorized build-isolation fix; release is not reproducible (`source_tree_dirty`).
6. B7: debug-connector pinout and voltage unverified.
7. Real-disk write path of `write_image_safe.ps1` is unexercised.

Owner authorization is required before: purchasing, flashing media, EEPROM changes, running anything on the board, or any source change to resolve B1/B4.
