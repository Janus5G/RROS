#!/bin/sh
# One-command AArch64 QEMU/UEFI virtual-platform validation (not Raspberry Pi hardware verification).
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
python3 tests/pi5_uefi_harness/harness.py "$@"
status=$?
latest=$(cat logs/pi5_uefi_harness/LATEST 2>/dev/null || echo "")
[ -n "$latest" ] && echo "Report: logs/pi5_uefi_harness/$latest/REPORT.md"
exit $status
