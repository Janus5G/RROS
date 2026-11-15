# Raspberry Pi 5 hardware validation - results record

Copy this file to `logs/pi5_hardware/<UTC date>_results.md` and fill it in with ACTUAL observations only.
Mark anything not performed as `NOT_RUN`, anything impossible as `BLOCKED (reason)`. Never edit a captured log.

## Session
| Field | Value |
|---|---|
| Date / operator | |
| Repository HEAD | |
| Package SHA256SUMS verified (Y/N) | |
| Release image SHA-256 written | |
| Board model / RAM / revision (`/proc/cpuinfo`) | |
| SoC stepping if known (C1 / D0) | |
| EEPROM bootloader version + date | |
| UEFI firmware release + SHA-256 (owner-reviewed) | |
| PSU / cooling | |
| Serial adapter + COM port | |

## Evidence index
| Test ID | Status (PASS/FAIL/BLOCKED/NOT_RUN) | Serial log path | SHA-256 | Notes |
|---|---|---|---|---|
| P0 vendor-OS baseline | | | | |
| F1 firmware alive on UART | | | | |
| F2 firmware loads EFI app | | | | |
| F3 RROS markers | | | | |
| U1 USB | | | | |
| E1 Ethernet | | | | |
| H1 HDMI | | | | |
| C1 camera | | | | |
| A1 audio | | | | |
| W1 wireless | | | | |

## Deviations, incidents, recovery actions taken
