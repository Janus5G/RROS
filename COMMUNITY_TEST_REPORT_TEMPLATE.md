# Raspberry Pi 5 hardware validation report

**Current gate: no physical operation is approved.** This historical report
does not authorize boot, connections, power-up, firmware changes or peripheral
tests. For current disconnected review, conditional electrical prerequisites,
blocked operations and a structured evidence template, see the
[English](docs/community/COMMUNITY_SAFETY.md),
[Danish](docs/community/COMMUNITY_SAFETY.da.md) and
[evidence guidance](docs/community/EVIDENCE.md). Leave unavailable or blocked
physical results NOT_VERIFIED/BLOCKED; never perform a test merely to fill a row.

**Do not claim overall hardware support from a probe or vendor-OS baseline.**
Submit through the [issue form](.github/ISSUE_TEMPLATE/pi5-hardware.yml).

## Identity and integrity

- Release tag/version and source commit:
- Archive filename, full SHA-256 and checksum verification command:
- Probe EFI filename and full SHA-256:
- Test date, duration and environment (physical Pi / QEMU / vendor OS):
- Board model, revision, BCM2712 C1/D0 stepping, RAM:
- PSU/cooling, HATs, PCIe devices, cables and peripherals:
- Firmware archive/version/SHA-256, EEPROM version/date:
- Boot medium, partition/filesystem layout and UEFI Device Tree/ACPI settings:

## Individually observed results

Use PASS, FAIL, BLOCKED or NOT_VERIFIED for each safety check, with exact
command where applicable, expected/actual result and reason. Existing test
tools may additionally report NOT_TESTABLE for unavailable software tests;
preserve their actual output. Until accepted physical evidence exists, status
is HARDWARE_NOT_VERIFIED.

| Check | Environment | Result | Evidence / reason |
|---|---|---|---|
| Vendor-OS baseline | | | |
| Firmware boot and app discovery | | | |
| UART / complete probe checks and END summary | | | |
| CPU / exceptions / MMU / memory / timer | | | |
| PCIe/RP1 FDT descriptions | | | |
| Actual PCIe link / BAR / MSI-X | | | |
| USB keyboard / mouse / storage | | | |
| Ethernet / DHCP | | | |
| Display / HDMI | | | |
| microSD / native storage | | | |
| Camera | | | |
| Audio | | | |
| Wi-Fi / Bluetooth | | | |
| GPIO / I2C / SPI | | | |
| Stability / observed temperatures | | | |

## Reproduction and attachments

- Exact safe reproduction steps and last successful marker:
- Full UART capture plus SHA-256; note redactions:
- Diagnostic parser output (including failures), FDT/baseline diagnostics:
- Optional genuine screenshots, photographs or video links, clearly labelled:
- Recovery actions / firmware or destructive changes performed:
- Known limitations encountered:

Confirm that backups and owner authorization covered any destructive changes.
Do not upload credentials, private keys, unnecessary identifiers or personal data.
