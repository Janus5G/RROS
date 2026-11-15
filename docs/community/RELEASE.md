# RROS v1 community development delivery

Version: **1.0.0-dev.2**. This is a source/build delivery, not certification of
physical Raspberry Pi 5 boot, peripherals, production robotics or safety.
The repository and all prepared downloads remain private pending owner approval.

## Downloads and reproduction

The private [Community package validation workflow](../../.github/workflows/community-package.yml)
produces the same named artifacts as the local builder:

| File | Contents |
|---|---|
| `RROS-1.0.0-dev.2-source.zip` | Redistributable source, build scripts, original test fixtures, provenance and internal checksums |
| `RROS-1.0.0-dev.2-software.zip` | Fresh x86-64 and generic AArch64 EFI builds; no firmware or physical Pi boot approval |
| `RROS-1.0.0-dev.2-SDK.zip` | SDK headers/examples, ABI definition and original development tools |
| `RROS-1.0.0-dev.2-Pi5-review.zip` | Safety gate, electrical checklist, BOM, illustrated review guide, offline tools and PDF |
| `RROS-1.0.0-dev.2-hardware-guide.pdf` | Community engineering-review guide; physical procedures blocked |
| `SHA256SUMS` / `release-manifest.json` | Download integrity, source revision, tool versions and build hashes |
| `RELEASE_NOTES.md` / `KNOWN_LIMITATIONS.md` | Scope, actual evidence and remaining blockers |

Download a completed private Actions artifact using `gh run download RUN_ID
--repo Janus5G/RROS --name RROS-v1-community --dir downloads`.
Artifacts expire after 30 days; this is not a published GitHub Release.
For long-term owner review, the same files are retained locally under
`build/community/1.0.0-dev.2`. Earlier versioned packages are preserved.
No third-party firmware or tool binary is bundled.

```powershell
Get-FileHash .\downloads\RROS-1.0.0-dev.2-source.zip -Algorithm SHA256
```

Compare against `SHA256SUMS` obtained with the same delivery. Hashes detect
corruption; an unsigned checksum is not an independent authenticity guarantee.
Inside WSL/Linux: `sha256sum -c SHA256SUMS`.

## Fresh checkout: Windows 11 + WSL2

1. Use an owner-authorized private checkout until public access is approved:
   `gh repo clone Janus5G/RROS`. Record `git rev-parse HEAD`.
2. Open Ubuntu 26.04 in WSL2. Use the Linux shell for native builds; Windows
   Python alone cannot build the freestanding EFI targets.
3. Use the pinned LLVM/LLD **21.1.8**, Ubuntu package version
   `1:21.1.8-6ubuntu1`. The rootless `sh scripts/bootstrap-toolchain.sh`
   downloads/extracts the explicit packages without installing them globally.
   The configured Ubuntu archive must provide those versions. Do not silently
   substitute another compiler. See [toolchain inventory](../TOOLCHAIN.md).
4. For repeatable dependency provisioning, use the digest-pinned disposable
   Ubuntu 26.04 container and dated Ubuntu snapshot in
   [provision-ci.sh](../../scripts/release/provision-ci.sh), exactly as the
   community workflow does. **Never run that provisioning script on a shared
   host**: it changes container package sources and requires root.
5. From the repository root, build the local delivery:

```sh
python3 scripts/release/community.py --build
python3 scripts/release/community.py --verify build/community/1.0.0-dev.2
```

The builder exports only the recorded Git commit, not dirty local source;
changes must be committed to be included. Outputs are isolated from accepted
release images. It builds each EFI target twice and compares the bytes.
An existing output directory is refused; use `--output NEW_DIRECTORY` for a
second delivery. No disk writer, board access or broad gate audit is invoked.

The source ZIP has the same layout as a checkout. Without `.git`, pass
`--source-tree` to rebuild from its checked internal file manifest; its
`SOURCE_PROVENANCE.json` retains the originating commit. The builder refuses
missing/changed source files rather than inventing a revision.

## Supported software checks

```sh
python3 -m venv .venv
.venv/bin/pip install --require-hashes -r tests/pi5_hw_validation/requirements.txt
.venv/bin/pip install --require-hashes -r tests/community-requirements.txt
.venv/bin/python tests/pi5_hw_validation/test_review_regressions.py
.venv/bin/python tests/unit/test_community_release.py
sh scripts/test-community-safety-host.sh
sh scripts/test-memory-host.sh
sh scripts/test-process-host.sh
sh scripts/test-storage-host.sh
sh scripts/test-network-host.sh
```

The seven CR groups remain independent in the existing release workflow.
The focused safety runner additionally requires a C17 host compiler with
pthread, ASan/UBSan and TSan support, as provided by the pinned CI toolchain.
The existing ARM64 workflow runs these applicable host suites on GitHub ARM64
Linux; it is **not physical Pi 5 testing**. The combined privileged G12 cache
suite remains NOT_TESTABLE on hosted ARM64 EL0. QEMU results, when cited,
describe virtual machines only. No new QEMU or G00-G22 acceptance is implied.

Captured RoboForge JSON and ChromaNeural vectors under `tests/gates/fixtures`
are excluded from the download because their redistribution clearance is
unresolved. Their dependent G15/G16/live-upstream checks are not supported by
this source-only delivery. Original parser fixtures under
`tests/pi5_hw_validation/fixtures` are included with provenance labels.
The PDF reference is removed and is never an input to this package.

## Hardware participation

Qualified volunteers may review source, diagrams, component selection and
already-collected evidence without the owner first owning a Pi 5.
Participation does not approve any physical operation. Read
[the safety gate](SAFETY_GATE.json), [hardware guide](HARDWARE_GUIDE.md) and
[evidence policy](EVIDENCE.md) before proposing a physical experiment.
Current safety-critical findings block RROS power-up, UART attachment,
firmware changes, media writing, DMA and peripheral testing as approved
community procedures. This is an engineering-review kit, not a ready-to-run
physical test kit. The offline demo/import tools do not open serial ports.

## Release readiness

No tag or GitHub Release is created by the builder or CI. The owner must
approve any publication or visibility change separately. The repository's
historical PDF and captured third-party fixtures remain in Git history;
do not distribute an unfiltered Git mirror as a cleared source package.
The filtered source archive does not resolve historical redistribution rights.
Original assessed RROS software is GPL-3.0-or-later; third-party components
retain their own terms in the [inventory](../LICENSE_INVENTORY.md).
