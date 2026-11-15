# Welcome to the RROS community

[Dansk](#dansk) | [Build and downloads](RELEASE.md) |
[Discussions](https://github.com/Janus5G/RROS/discussions) |
[Issues](https://github.com/Janus5G/RROS/issues) |
[Pull requests](https://github.com/Janus5G/RROS/pulls)

Welcome to contributors of every experience level. Ask questions, share
original builds, propose ideas, document failures and help improve source
and documentation. The former repository was deleted; the links above are
historical and will not work until a destination is established. Creation,
visibility and external access to a future repository remain owner decisions.

## Choose a channel

- **Discussions:** questions, community builds, photographs and videos,
  safety/compatibility review, ideas and development conversation. Until
  custom categories are created, use Q&A, Show and tell, Ideas and General;
  [category setup](DISCUSSIONS.md) lists the exact mapping.
- **Issues:** reproducible bugs or structured hardware safety/test reports.
  A discussion can lead to a linked issue when there is an actionable defect.
  Do not post sensitive security details; follow [SECURITY.md](../../SECURITY.md).
- **Pull requests:** proposed code or documentation changes. Link the issue
  or discussion, list focused tests and invite review. A proposal is not an
  accepted change until reviewed and merged.

In a discussion, comment or reply to discuss solutions. In a Q&A category the
author or a maintainer can mark a helpful answer; open an issue for a
reproducible defect rather than treating an unanswered question as one.
For a change, follow [CONTRIBUTING.md](../../CONTRIBUTING.md) and the
[pull request template](../../.github/pull_request_template.md). Follow the
[code of conduct](../../CODE_OF_CONDUCT.md).

## Attachments and privacy

GitHub provides native attachments; no external host or email is required.
See [GitHub's attachment documentation](https://docs.github.com/en/get-started/writing-on-github/working-with-advanced-formatting/attaching-files).

1. Prepare **copies** and inspect/redact every file and ZIP member before
   selecting it: passwords, tokens, encryption keys, personal information,
   unnecessary serial numbers and network identifiers must be removed.
   Avoid private data, firmware binaries and material without permission.
2. In an issue, pull request or discussion comment, drag a file into the
   editor or use its attachment control. Images can often be pasted. For a
   form that does not accept a file directly, submit the text and attach it
   in a comment. GitHub uploads when a file is attached, **before** the
   comment is submitted; do not attach an unreviewed original.
3. Wait for the upload, use the Markdown preview where available, verify the
   file/link and label its environment, source, redactions and SHA-256 of
   the submitted copy. Then post the comment. The preview does not prove a
   video works in every browser; GitHub recommends H.264 for compatibility.

GitHub documents PNG/JPEG and SVG, MP4/WebM video, and, in issue/PR/discussion
comments, `.log`/`.txt`, `.json`, `.pdf`, `.zip` and `.drawio` files.
Genuine RROS desktop screenshots and hardware photographs are welcome when
available; label vendor OS, firmware, simulation and concepts accurately.
GitHub currently documents 10 MB per image; 10 MB per video on free plans
(100 MB on paid plans, subject to eligibility); and 25 MB per other file.
Check the linked GitHub documentation for current limits and supported
contexts. No upload or email is automatic.

Attachments in a private repository are accessible to people with
repository access. **In a public repository uploaded files may be accessible
without authentication.** An anonymized URL is not a privacy guarantee.
Only share material you have permission to publish.

## Hardware and evidence

Read the [English safety guide](COMMUNITY_SAFETY.md) or
[Danish safety guide](COMMUNITY_SAFETY.da.md) and the existing
[safety gate](SAFETY_GATE.json). Disconnected inspection, source review and
supported hosted tests are welcome under their documented prerequisites.
Physical RROS boot, connections, power-up, DMA and peripherals are currently
blocked. A submitted measurement is evidence for independent review, not
automatic approval. Use the [structured evidence template](EVIDENCE.md).

Record SOFTWARE_PASS only for tested software; HARDWARE_PASS requires actual
reviewed physical evidence and the applicable approved procedure. Record
FAIL, BLOCKED or NOT_VERIFIED honestly. QEMU and hosted ARM64 are not Pi 5
hardware tests.

## Dansk

Velkommen! Det tidligere repository er slettet; links er historiske, indtil
ejeren har valgt en ny destination. Brug derefter Discussions til
spørgsmål, byggerier, billeder, video og idéer; brug Issues til
reproducerbare fejl og sikkerhedsrapporter og Pull Requests til konkrete
ændringer. I Q&A kan spørgeren eller en maintainer markere et svar. Link
mellem diskussion, issue og pull request; en idé er ikke en godkendt ændring.

GitHubs egne vedhæftninger kan bruges uden ekstern filhost eller e-mail:
træk filen til kommentarfeltet eller brug vedhæftningsknappen, vent på
upload, kontrollér forhåndsvisning/link og beskriv kilde og miljø. Hvis en
formular ikke accepterer filen, tilføj den i en kommentar. Upload sker
allerede ved vedhæftning, før kommentaren sendes. Slet adgangskoder, tokens,
krypteringsnøgler, personoplysninger og unødige serie-/netværksidentifikatorer
fra alle filer og ZIP-indhold først. Filer i offentlige repositories kan være
offentligt tilgængelige. Format- og størrelsesgrænser står i
[GitHubs dokumentation](https://docs.github.com/en/get-started/writing-on-github/working-with-advanced-formatting/attaching-files).

Læs [den danske sikkerhedsvejledning](COMMUNITY_SAFETY.da.md) og
[evidensskabelonen](EVIDENCE.md). Frakoblet inspektion og softwaretests kan
ske efter de angivne forudsætninger; fysisk RROS-opstart, tilslutning,
strøm, DMA og periferiudstyr er aktuelt blokeret. Indsendte målinger er ikke
automatisk godkendelse, og SOFTWARE_PASS er ikke HARDWARE_PASS.
