# Security in RoboticRasOS (RROS)

This document describes the current security model and its verified boundaries.
Evidence comes from host tests (plain and ASan/UBSan) and QEMU guests; physical
Raspberry Pi 5 validation is pending. RROS is not currently a production security
boundary.

## Implemented (software-verified)

- Accounts with PBKDF2-HMAC-SHA256 password hashing, per-account salt, lockout, roles
  (GUEST < USER < OPERATOR < ADMIN < OWNER, plus SERVICE), escalation rules, quotas and a hash-chained audit ring
  (`kernel/secsvc.c`).
- ChaCha20 (RFC 8439 known-answer verified) used for simulated peer-transfer frames (`kernel/chacha20.c`).
- Simulated three-node deployment (`kernel/secdeploy.c`): peer enrollment/revocation, signed package envelope,
  replay protection, architecture/dependency/version-floor/quota checks, recipient-approved installation bound
  to the package digest, journaled install with automatic rollback, authorized rollback with a downgrade floor.
- Process isolation, W^X metadata, and capability checks in earlier gates; MCP access is local-only,
  quota-limited and treats tool output as untrusted data.

## Documented peer enrollment and install/rollback interfaces

- Enrollment: an OWNER/ADMIN enrolls a peer by identity and key (`secd_enroll_peer`); revocation
  (`secd_revoke_peer`) blocks further frames. Packages are accepted only from enrolled peers.
- Installation: `secd_receive` stages a verified package, the recipient approves its digest
  (`secd_approve`), then `secd_install` applies it. Rollback uses `secd_rollback` and is refused below the
  version floor.

## Current boundaries

- Signatures are HMAC-SHA256 with a shared key: symmetric, so any verifier could forge. Public-key signatures
  are Not implemented.
- No hardware entropy source: salts, secrets and nonces are caller-supplied entropy.
- Installs are metadata-only; the network is simulated. No TLS, key exchange or forward secrecy.
- Rollback policy and audit log are not persisted; stale-state defense needs an externally held monotonic
  counter (simulated by an argument).
- No secure boot, no hardware root of trust. Release artifacts are unsigned; verify SHA-256 manifests only.
- No private keys or credentials are included in any release artifact; JSON manifests are data, never code.
