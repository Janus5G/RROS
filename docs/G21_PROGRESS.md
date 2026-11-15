# G21 — System-Wide Integration

Status: SOFTWARE_PASS (host simulation, QEMU regression through earlier gates). Hardware: HARDWARE_NOT_VERIFIED.

## Implemented

- `tests/gates/G21_integration_test.c` (run by `scripts/test-integration-host.sh`, plain and ASan/UBSan):
  three simulated nodes (x86_64, x86_64, aarch64) combine `secdeploy` with per-node `os_storage` on a RAM
  block device. Node state (installed packages, downgrade floors, generation) is persisted sealed with an
  HMAC, restored after restart, and refused when tampered (every stored byte flipped), keyed wrongly,
  missing, or stale (generation floor). Downgrade floors survive restart; the rollback policy is not
  persisted, so rollback fails closed after restart. User privilege boundaries and audit chains are checked.
- Reproducibility fix: `ld.lld -flavor link /brepro` in both build scripts removes PE timestamps. Two clean
  builds of each architecture are now bit-identical (checked by `tests/gates/G21_test.py`).
- Dependency audit: kernel/arch/boot sources include only `<stddef.h>`/`<stdint.h>`; the single `<stdio.h>`
  in `kernel/desktop.c` is guarded by `EXPERIMENTALOS_DESKTOP_TEST_DIAGNOSTICS`. No submodules, vendored
  upstream trees, or network fetches in build scripts.
- Full regression: `G21_test.py` runs every G00–G20 acceptance test (clean builds, QEMU, host tests).

## Limits (honest, carried from G20)

- Signatures are HMAC-SHA256 with a shared key: symmetric, so any verifier could forge. Public-key
  signatures are Not implemented.
- No hardware entropy: salts, secrets and nonces are caller-supplied entropy. Real entropy is
  HARDWARE_NOT_VERIFIED.
- Installs are metadata-only; the network is simulated; rollback policy and audit are not persisted.
- Stale-state defense needs an externally held monotonic counter; here it is simulated by the `min_gen`
  argument.
- Not implemented: key exchange, TLS, secure boot, persistent accounts.
- Raspberry Pi 5 behavior is HARDWARE_NOT_VERIFIED; platform support remains compatibility-pending.
