# Security policy

## Current scope

RoboticRasOS (RROS) v1 is being prepared for community development; repository
visibility remains private until owner approval. There is no published
release with a production security-support commitment. Security reports are
evaluated against the current development branch and relevant test evidence.

The [technical security documentation](docs/SECURITY.md) describes implemented
controls and their limits. Host and QEMU tests do not establish a secure
deployment on physical Raspberry Pi 5 hardware.

## Reporting a concern

Authorized reviewers may open an issue in this private repository with an
initial, redacted description. For sensitive findings, contact the repository
owner through an existing private project channel before attaching exploit
details or confidential material.

Include the affected revision, relevant component, expected and observed
behavior, and a minimal software reproduction where possible. Do not include
credentials, private keys, personal data or unnecessary device identifiers.
Do not reproduce a finding on physical equipment without its owner's consent.

No dedicated security email or public disclosure service is advertised.
Do not post sensitive exploit details or personal data in public collaboration
threads. Coordinate responsible disclosure with maintainers through an existing
private channel. Original RROS software is GPL-3.0-or-later; this policy does
not add restrictions to that license or relicense third-party material.

## Handling and verification

Findings are assessed against the documented threat model. Corrections should
include focused regressions, preserve prior evidence and distinguish software
verification from physical observations. A passing check is evidence for its
tested behavior, not a blanket security guarantee.

The [community safety gate](docs/community/SAFETY_GATE.json) currently blocks
physical RROS execution and peripheral tests. Security review on a host does
not clear electrical, DMA, IRQ or physical-device risks.
