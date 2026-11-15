# G22 — Experimental Release Candidate

Status: SOFTWARE_PASS (local release candidate 0.1.0-rc1). Hardware: HARDWARE_NOT_VERIFIED. Not published.

## Produced (local, `sh scripts/build-release.sh`)

- `build/x86_64/ExperimentalOS.iso` (El Torito EFI + GPT ESP; booted in an OVMF/QEMU guest).
- `build/aarch64/ExperimentalOS-Pi5.img` (GPT + FAT32 ESP). Structurally checked by the independent
  `tools/checkimage.py` and booted as a generic AArch64 UEFI guest in QEMU `virt`. It is not boot-verified on
  Raspberry Pi 5 and no Pi UEFI firmware chain is bundled.
- Native build artifacts, SDK package, Development & Deployment Manager resources package (separate),
  `build/manifests/` (SHA256SUMS, release and package manifests), `build/test-results/`.
- Documents: [known limitations](KNOWN_LIMITATIONS.md), [security](SECURITY.md),
  [hardware validation guide](HARDWARE_VALIDATION_GUIDE.md), [reproducible build](REPRODUCIBLE_BUILD.md),
  [release notes](RELEASE_NOTES.md).
- Tools: `tools/mkimage.py` (deterministic FAT/GPT), `tools/checkimage.py`, `tools/release_manifest.py`.

## Verification (actual)

`tests/gates/G22_test.py` builds the release twice (all artifacts bit-identical), structurally validates the
Pi image (and proves the checker rejects corruption and wrong architecture), inspects the ISO, boots both
images in QEMU, verifies every manifest hash, and checks that no private keys or fake files are shipped.

## Limits (honest)

- Not implemented: `kernel-debug.elf` (kernel is a PE/COFF UEFI app) and a Development & Deployment Manager
  executable; only contracts, sources and simulation exist. Both are recorded as absent in the manifest.
- Raspberry Pi 5 behavior is HARDWARE_NOT_VERIFIED; platform support remains compatibility-pending.
- G20 limits stand: symmetric HMAC signatures, caller-supplied entropy, metadata-only installs, simulated
  network. Artifacts are unsigned; trust is by SHA-256 manifest only.
- Nothing is pushed, published or flashed; that requires explicit owner authorization.
