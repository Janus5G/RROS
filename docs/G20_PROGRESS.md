# G20 — Advanced Security and Resilience

Status: SOFTWARE_PASS (host simulation). Hardware: HARDWARE_NOT_VERIFIED.

## Implemented (kernel modules, freestanding C)

- `kernel/secsvc.{h,c}`: accounts (PBKDF2-HMAC-SHA256, per-account salt, password policy, lockout),
  HMAC-protected session tokens with expiry, roles GUEST < USER < OPERATOR < ADMIN < OWNER plus SERVICE,
  escalation rules (only strictly lower roles can be created or modified; no self-modification; single owner
  bootstrap), service identities (single-use challenge-response, permission ceiling), storage and
  request-rate quotas with overflow-safe accounting, and a hash-chained audit ring with tamper detection.
- `kernel/chacha20.{h,c}`: RFC 8439 ChaCha20 (known-answer verified).
- `kernel/secdeploy.{h,c}`: simulated deployment node: peer enrollment/revocation, signing-key registry
  (key identity, validity window, revocation wiping key material; revocation re-checked at install),
  signed package envelope (SHA-256 + HMAC-SHA256), encrypted authenticated frames with sequence replay
  protection, architecture/dependency/version-floor/quota checks, bounded staging and discard, approval bound
  to package digest with expiry, journaled install with crash-recovery and automatic rollback on failed
  verification, authorized rollback with a version floor that blocks downgrade, and audit of every decision.

## Verification (actual)

- `scripts/test-security-host.sh` builds `tests/gates/G20_security_test.c` with `-Wall -Wextra -Werror` and
  with ASan/UBSan; all checks pass in both builds. Gate test `tests/gates/G20_test.py` requires every check id
  to appear in both builds.
- Three simulated nodes (x86_64, x86_64, aarch64) plus an attacker node cover: unauthorized senders, malformed
  JSON, tampering, invalid signatures, incompatible architecture, missing dependencies, capability denial,
  interruption, failed installation, rollback and authorized activation.

## Limits (honest)

- Signatures are HMAC-SHA256 with a shared key: symmetric, so any verifier could forge. Not a public-key
  scheme. Public-key signatures are Not implemented.
- No hardware entropy source: salts, session secrets and nonces come from caller-supplied entropy
  (all-zero rejected). Real entropy is HARDWARE_NOT_VERIFIED.
- Deployment is a simulation: no real network transport, installs are metadata-level (no payload executed or
  written to storage), app-image packages with architecture "any" are refused.
- Not implemented: key exchange/forward secrecy, persistent account or audit storage, TLS, secure boot.
- Raspberry Pi 5 behavior is HARDWARE_NOT_VERIFIED; platform support remains compatibility-pending.
