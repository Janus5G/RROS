# Raspberry Pi 5 validation

Qualified community members may contribute engineering review and offline
evidence without the owner first validating a board. Start with the
[community guide](docs/community/HARDWARE_GUIDE.md),
[BOM](docs/community/BOM.csv) and [safety gate](docs/community/SAFETY_GATE.json).
Current safety-critical requirements block physical connections, power-up,
RROS boot and peripheral testing. No historical procedure below overrides
that gate. The downloadable kit is an engineering-review package.

Physical Raspberry Pi 5 validation is pending. The current private review
provides software evidence and a separate diagnostic-probe workflow; it does
not establish board or peripheral readiness.

Read the [English guide](RASPBERRY_PI5_TEST_GUIDE.md) or
[Danish guide](RASPBERRY_PI5_TEST_GUIDE.da.md), the
[compatibility matrix](HARDWARE_COMPATIBILITY_MATRIX.md), and
[report template](COMMUNITY_TEST_REPORT_TEMPLATE.md).
The [GitHub issue form](.github/ISSUE_TEMPLATE/pi5-hardware.yml) collects the same fields.

## Diagnostic probe

The [probe-package builder](scripts/pi5_hw/build_probe_package.py) assembles
the separate firmware-aware EFI diagnostic program and its capture/evaluation
tools. The [testing guide](RASPBERRY_PI5_TEST_GUIDE.md) covers package
verification and firmware research. Its physical steps are historical and
blocked by the current safety gate. This is not
the complete OS image or a ready-to-install Raspberry Pi OS replacement.

## Recording evidence

Evidence classes:

- **PASS / FAIL:** an executed, named software or physical check, with its
  environment and evidence. A subsystem PASS is not an overall board PASS.
- **NOT_TESTABLE:** unavailable/incomplete driver, unsuitable firmware, missing
  equipment or a test the environment cannot execute; state the exact reason.
- **HARDWARE_NOT_VERIFIED:** no accepted physical evidence. This remains true
  even when the generic AArch64 emulator or diagnostic probe passes.

Keep a stock vendor-OS baseline, firmware boot, diagnostic-probe result and
native RROS peripheral result in separate sections. Submit unedited UART logs
with hashes; redact sensitive information before public upload and record that
redaction occurred. A log parser is an aid, not an independent hardware witness.
Maintainers must review board identity, artifact checksum and actual observations
before accepting a hardware result. No global status is upgraded automatically.
