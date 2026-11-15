# Contributing to RoboticRasOS (RROS)

Good contributions make a small part of the system clearer, more reliable
or easier to reproduce. RROS v1 is being prepared for open-source community
development. The repository currently remains private; public access and
publication still require the owner's separate decision.

## Choose a focused contribution

- **Code:** a bounded correction, backed by the smallest relevant existing tests.
- **Documentation:** clearer interfaces, accurate examples and aligned translations.
- **Software evidence:** focused, reproducible regression results.
- **Visuals:** original diagrams or genuine captures with recorded provenance.

Start with the [architecture](docs/ARCHITECTURE.md),
[current limitations](docs/KNOWN_LIMITATIONS.md) and
[verification guide](docs/release/TESTING.md).
For a fresh build and filtered source download, use the
[community development guide](docs/community/RELEASE.md).

## Propose a change

1. Describe the problem and intended behavior in an issue or pull request.
2. Keep the branch focused; preserve existing interfaces and accepted evidence.
3. Run the relevant host, guest or tooling checks. Record the source revision,
   commands, environment, exit codes and artifact hashes where applicable.
4. Update related documentation. Keep the English and Danish front pages aligned.
5. State dependency licenses and the provenance of any new assets.

Use targeted regressions rather than repeating a broad audit for a small
change. Keep genuine failures visible; do not weaken tests to obtain a pass.
The seven Code Review corrections and their independent CI results are
recorded in the [verification guide](docs/release/TESTING.md).

## Hardware scope

Qualified volunteers may contribute engineering reviews and, only after the
relevant safety gate is approved, physical evidence. The owner is not
required to validate hardware personally before community participation.
The current [safety gate](docs/community/SAFETY_GATE.json) has unresolved
critical requirements: no physical connection, power-up, RROS boot or
peripheral test is currently approved. Start with
[the engineering-review guide](docs/community/HARDWARE_GUIDE.md) and
[privacy-aware evidence instructions](docs/community/EVIDENCE.md).
No contribution authorizes robot or PLC actuation.

## Community channels

Start with the [community guide](docs/community/COMMUNITY.md) for native
attachments, privacy, category navigation and Danish guidance.
Use [Discussions](https://github.com/Janus5G/RROS/discussions) for questions,
build experiences, architecture, ideas and engineering review; use Issues
for reproducible defects and Pull Requests for proposed changes.
Screenshots, photographs and logs are welcome when genuine, correctly labeled,
voluntary and cleared for sharing. Review and redact archives before upload.
Follow the [code of conduct](CODE_OF_CONDUCT.md). No real name, email,
private account data, secret or personal device identifier is required.

## Protect the project boundaries

Keep AI/MCP output untrusted and outside privileged interfaces. Do not expose
private endpoints or include credentials, signing keys or personal data in
logs. Follow the [security policy](SECURITY.md) for security concerns.

The repository remains private. Public access, external invitations and release
publication are separate owner decisions. Original assessed RROS code is
[GPL-3.0-or-later](LICENSE); retain its SPDX notices and submit original
contributions under that license. Do not copy incompatible or uncleared code.
Review [third-party notices](THIRD_PARTY_NOTICES.md) and the
[component inventory](docs/LICENSE_INVENTORY.md); third-party terms remain separate.
