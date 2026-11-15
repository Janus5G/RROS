# ExperimentalOS 0.1.0-rc1 release notes (local experimental release candidate)

Not published, not pushed, not flashed. HARDWARE_NOT_VERIFIED.

## Contents (under `build/`)

| Artifact | Status |
|----------|--------|
| `x86_64/ExperimentalOS.iso` | UEFI ISO; booted in an OVMF/QEMU guest. Physical boot not verified. |
| `aarch64/ExperimentalOS-Pi5.img` | GPT + FAT32 ESP; structurally checked and booted as a generic AArch64 UEFI guest in QEMU only. Not Pi 5 boot-verified. |
| `x86_64/esp/EFI/BOOT/BOOTX64.EFI`, `aarch64/esp/EFI/BOOT/BOOTAA64.EFI` | Native build artifacts (PE/COFF UEFI applications). |
| `sdk/ExperimentalOS-SDK-0.1.0-rc1.tar.gz` | SDK headers, examples, host tools, documentation. |
| `sdk/RPi5-DevDeploy-Manager-Resources-0.1.0-rc1.tar.gz` | Manager contracts/sources/docs. The manager executable is Not implemented. |
| `manifests/` | `SHA256SUMS`, `release-manifest.json`, `package-manifest.json`. |
| `test-results/` | Gate result reports G00-G21 copied from `logs/`. |
| `kernel-debug.elf` | Absent: the kernel is linked as a PE/COFF UEFI application. |

## Documents

[Known limitations](KNOWN_LIMITATIONS.md), [security](SECURITY.md),
[hardware validation guide](HARDWARE_VALIDATION_GUIDE.md), [reproducible build](REPRODUCIBLE_BUILD.md).
No project license has been selected; no third-party runtime code is bundled.
