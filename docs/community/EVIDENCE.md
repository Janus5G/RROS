# Community testing and evidence

After the owner creates and configures the future repository, use Discussions
for collaboration, Issues for reproducible defects and Pull Requests for
changes. No historical discussions or previous release evidence are included.

Submit only evidence from an operation authorized by its relevant safety
gate. The current [hardware gate](SAFETY_GATE.json) approves **no physical
RROS boot, connection, power-up, DMA or peripheral test**. Qualified
volunteers can contribute source/electrical reviews and offline log analysis.

## Minimum report

Copy this template for an existing check, not as an instruction to perform
a blocked operation. Use the existing E01-E10 and SW-01–SW-05/HW-01–HW-03
identifiers where applicable; see [English](COMMUNITY_SAFETY.md) or
[Dansk](COMMUNITY_SAFETY.da.md) for permitted review and prerequisites.
Missing information is **NOT_VERIFIED**, not an empty implied PASS.

### Identity and scope

- Existing requirement/check ID and review level (A/B/C):
- Status: PASS / FAIL / BLOCKED / NOT_VERIFIED; reason:
- Release version, source commit, package filename and verified SHA-256:
- Environment: disconnected review / native host / QEMU / vendor OS /
  firmware / probe / native RROS; identify synthetic data explicitly:
- Board model, PCB revision, RAM and stepping, if known:
- Component quantities and exact model/revisions/specifications: PSU and
  profile, cable, cooler, enclosure, debug adapter and other accessories:
- Firmware/media versions and hashes, if already available without a blocked
  operation; do not upload firmware:
- Reference documents, revisions, relevant figures/limits and access dates:

### Procedure, observations and independent review

- For any separately authorized inspection/measurement: procedure identifier,
  revision, scope, prior approval and prerequisites; reviewer role and date
  without requiring personal contact information:
- Proposed pin/connector orientation, signal/reference, polarity, voltage and
  current limits, backfeed risk and the applicable reviewed diagram:
- Instrument type/ID, rating and range; measurement points, conditions,
  observed value and units, uncertainty, pre-established acceptance limits,
  outcome and qualified independent review:
- Expected versus actual result, exact command and applicable test/log name;
  include compiler/tool versions and host OS/architecture for software:
- Failures, contradictions, stop conditions and remaining NOT_VERIFIED items:
- Optional genuine photographs or screenshots, with captions, environment,
  date and redactions; mark conceptual or synthetic material explicitly:
- Evidence/log filenames and SHA-256 for the **submitted redacted copies**;
  record what was removed. Original evidence remains local.
- Independent engineering assessment and project safety-gate disposition,
  if available: reviewer role, date, reviewed scope and any explicitly
  approved next operation. A contributor's measurement alone is not approval.

## Privacy and consent

For GitHub-native attachment formats, sizes and preview steps, see the
[community guide](COMMUNITY.md#attachments-and-privacy). Upload begins when
the file is attached, before the comment is posted. Public-repository
attachments may be accessible without authentication.

Keep the original evidence locally. Before uploading, inspect every text,
JSON and ZIP member. Remove secrets, tokens, account names, personal paths,
network identifiers, device serials and contact details that are unnecessary
to the reproduction. Document redactions so hashes refer to the submitted
copy. Automated collection does not imply consent to upload.

Never attach private upstream data, firmware binaries, full disk images,
third-party copyrighted documents or logs belonging to someone else without
permission. Do not run physical experiments merely to fill a report template.
No private email address or real name is required to participate.
