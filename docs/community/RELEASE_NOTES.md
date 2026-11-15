# RoboticRasOS v1 — 1.0.0-dev.2

Private owner-review draft. This delivery prepares original RROS source,
reproducible software builds and community engineering review. It does not
approve a public launch or physical Raspberry Pi 5 execution.

## Separate downloads

- Source/development ZIP, complete with build scripts, supported original
  regression fixtures, GPL license, notices and commit provenance.
- Software ZIP: freshly built x86-64 and generic AArch64 EFI outputs.
- SDK ZIP: headers, examples, ABI definition and original host tools.
- Pi 5 engineering-review ZIP: BOM, safety gate, illustrated PDF and runnable
  offline evidence tools. No serial capture or board power-up is approved.
- Standalone community hardware guide PDF.
- SHA256SUMS, release-manifest.json and KNOWN_LIMITATIONS.md.

Read the [fresh-checkout/build instructions](https://github.com/Janus5G/RROS/blob/@SOURCE_COMMIT@/docs/community/RELEASE.md).
The source revision and precise artifact digests are recorded in the manifest.
No third-party firmware, tool binary or copyrighted reference PDF is bundled.

## Verification boundaries

The packager compares two isolated builds per EFI target. The community
workflow verifies internal/outer checksums, repeated package generation,
rebuilding the source ZIP without Git metadata, offline report generation,
serial refusal and independently parsed PDF contents.

The existing seven CR-01–CR-07 jobs and native ARM64 host tests remain separate
and authoritative in GitHub Actions. Hosted ARM64 is not Pi 5 board testing.
No new QEMU boot or G00-G22 acceptance is claimed by this delivery.

## Hardware gate and limitations

Physical RROS boot, UART attachment, power-up, media writes, firmware changes,
DMA and peripheral operations are BLOCKED by unresolved critical requirements.
The recorded probe map-validation order, UART fan-out and page-allocation
concurrency defects are corrected with focused regressions. Malformed pool
metadata is also rejected. Strict C17, ASan/UBSan and TSan tests establish
software behavior only. See SAFETY_REVIEW.md for exact scope and the separate,
unresolved whole-driver USB analyzer observation.

Target-specific startup, actual register/IRQ/DMA cache coherency, firmware
compatibility and electrical measurements still require engineering review.
ELECTRICAL_PRE_POWER.md supplies the evidence checklist; all unmeasured checks
remain NOT_VERIFIED. No physical operation has been approved.

The ordinary AArch64 EFI build is a QEMU/development artifact, not a physical
Pi 5 image. USB, Ethernet, display, camera, audio, wireless and storage have no
new physical verification. The hardware ZIP/PDF is for independent engineering
review and offline rehearsal, not an approved power-up procedure.

## Licensing and publication blockers

Original assessed RROS software is GPL-3.0-or-later; third-party terms remain
separate. Captured upstream fixtures are excluded from the source download;
their dependent upstream checks are not part of this package's supported
software-test set. Historical PDF/fixture copies still exist in Git history;
do not distribute an unfiltered mirror as cleared source. Exact firmware/DTB
redistribution clearance and physical safety approval remain unresolved.

Repository visibility is private. Public access/release requires separate
owner approval. No external collaborator invitations are part of this draft.
