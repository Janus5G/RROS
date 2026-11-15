# Release readiness

## Public-facing RROS preparation

The new candidate is `0.1.0-experimental.1`; see
[release notes](release/RELEASE_NOTES.md), [staged tests](release/TESTING.md)
and [publication checklist](release/PUBLISHING.md).
Historical `0.1.0-rc1` artifacts/evidence below are preserved, not reinterpreted.
The owner subsequently authorized a private first release at `Janus5G/RROS`,
including commits, pushes and CI. Visibility must remain PRIVATE. No public
release, collaborator invitations or physical test is authorized.
No project license has been chosen; firmware/reference redistribution rights
remain public-distribution blockers.

**Status: local experimental release candidate 0.1.0-rc1 (G22 SOFTWARE_PASS). Not published, not flashed, HARDWARE_NOT_VERIFIED.**

See [release notes](RELEASE_NOTES.md), [known limitations](KNOWN_LIMITATIONS.md), [security](SECURITY.md) and the
[hardware validation guide](HARDWARE_VALIDATION_GUIDE.md). Raspberry Pi packaging is structurally validated only; the
Pi 5 image is not boot-verified.

Release conditions that still apply:


- G00-G22 applicable software gates must pass with source-linked reports.
- x86-64 must boot as an actual QEMU guest; AArch64 artifacts must pass
  applicable emulator tests.
- Raspberry Pi packaging must be structurally validated; physical status must
  be stated accurately.
- Dependency provenance, licenses/notices, artifact hashes and recovery steps
  must be complete.
- Security and known limitations must be documented.
- No publishing or physical flashing occurs without separate owner
  authorization.
