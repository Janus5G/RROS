# RoboticRasOS (RROS) release candidate

Version plan: `0.1.0-experimental.1` · Private pre-release review.

The first candidate is being prepared for private evaluation. A complete
candidate still requires acceptance and packaging; no release is published
and public distribution is not approved.

RoboticRasOS (RROS) is an independently developed native operating system
designed for robotics, AI applications and general-purpose computing on
AArch64 and x86-64. Software regression checks and a separate Pi 5 diagnostic
probe support validation; physical board and peripheral verification remain pending.

## Planned artifact set

These names define the release pipeline's intended outputs, not currently
available downloads or a statement that a complete candidate has passed.

| Exact filename | Contents / intended use |
|---|---|
| `RROS-0.1.0-experimental.1-x86_64.tar.gz` | Freshly built x86-64 UEFI ISO and EFI executable; QEMU/OVMF evaluation |
| `RROS-0.1.0-experimental.1-aarch64-qemu.tar.gz` | Fresh GPT/FAT32 `ExperimentalOS-Pi5.img` and EFI executable; generic AArch64 QEMU/AAVMF only |
| `RROS-0.1.0-experimental.1-Pi5-probe.tar.gz` | Separate firmware-aware diagnostic EFI app, capture/evaluation tools, English/Danish instructions and package hashes; no third-party firmware |
| `RROS-0.1.0-experimental.1-SDK.tar.gz` | Existing deterministic ExperimentalOS SDK format; retains internal `0.1.0-rc1` naming/ABI |
| `RROS-0.1.0-experimental.1-DevDeploy-resources.tar.gz` | Contracts, sources and deployment simulations; no manager executable |
| `RROS-0.1.0-experimental.1-source.tar.gz` | Current source snapshot and release docs; excludes reference PDF, private agent configuration, toolchains, upstream checkouts and build outputs |
| `RROS-0.1.0-experimental.1-evidence.tar.gz` | Fresh gate reports, exact command logs, QEMU/probe evidence and source-linked checks; not physical Pi evidence |
| `release-manifest.json` | Full source commit/snapshot digest, dirty state, per-artifact sizes/hashes, software outcomes and publication blockers |
| `SHA256SUMS` | SHA-256 of all release files including the manifest; no signing claim |

The source archive intentionally omits `OSBuild.pdf` pending rights review:
the unchanged G00 reference audit requires the owner-cleared full checkout,
not that public archive alone. Historical accepted artifacts remain unchanged
and are not silently repackaged as verified at this new HEAD.

## Validation

The [staged procedure](TESTING.md) runs test-tool self-tests, G00-G20,
source-bound G21 integration, two clean payload builds with exact equality,
G22 artifact/negative-corruption checks and actual x86-64/generic AArch64
QEMU boots. It also runs the AArch64 UEFI CPU/GIC/memory/device matrix and
firmware-derived console/probe tests using pinned firmware/DTB inputs.

PASS means an executed software test passed its existing assertions.
FAIL is retained and blocks a successful candidate. NOT_TESTABLE names
checks unavailable to the environment. Physical Pi status remains
HARDWARE_NOT_VERIFIED even with all software checks passing. Workflow
definitions are locally validated; a GitHub-hosted run is not claimed until
it actually occurs.

## Important limitations

- No accepted physical Pi 5 boot, peripheral, performance or stability evidence.
- The complete AArch64 image is not a Pi-ready firmware card and still has
  the legacy QEMU UART path; use the separate diagnostic probe for bring-up.
- Archived Pi UEFI v0.3 must be obtained independently and hash-checked.
  Device Tree mode is required; C1/D0/display/Ethernet caveats apply.
- G12 acceptance is conditional, fixture-level. Native USB/Ethernet/display/
  camera/audio/wireless functionality is incomplete.
- Native hosted ARM64 G12 cache tests are NOT_TESTABLE at EL0; x86 register
  models and privileged AArch64 QEMU fixtures remain required.
- RoboForge/ChromaNeural are private upstreams. Full pinned upstream checks run
  locally; CI has no cross-repository authority and reports these two checks
  NOT_TESTABLE while executing the committed contract/vector fixtures.
- Many desktop/network/browser/language/services are bounded fixtures or
  subsets, not a production application platform.
- Deployment uses symmetric HMAC signatures, caller-supplied entropy,
  simulated transport and metadata-only installs/rollback. No production
  encrypted network transfer or independently secure signing system is claimed.
- No `kernel-debug.elf`, signed release payload or deployment-manager executable.
- Historical adjacent EFI/manifest drift and probe/serial-evaluator boundaries
  are retained in [testing](TESTING.md). The seven Code Review corrections have
  independently passing targeted CI, not full release qualification.
- Original SVG branding and bilingual architecture illustrations are included.
  Authentic desktop screenshots and physical demonstrations remain absent.

## Independent testing

Follow the [English](../../RASPBERRY_PI5_TEST_GUIDE.md) or
[Danish](../../RASPBERRY_PI5_TEST_GUIDE.da.md) guide. Verify hashes, use
disposable media and obtain owner approval for destructive/firmware actions.
Submit individual results with board/firmware/version/SHA-256 and full UART
logs through the [issue form](../../.github/ISSUE_TEMPLATE/pi5-hardware.yml).
Firmware/probe success is not overall Pi support.

## Outstanding public-distribution blockers

Public-distribution approval; no project LICENSE; unresolved reference-PDF and
firmware redistribution rights. Private first-stage repository identity is
`Janus5G/RROS`; it must remain PRIVATE. Candidate changes must be committed,
tested and tagged before the private prerelease is created.
See [exact publication commands](PUBLISHING.md) and
[third-party notices](../../THIRD_PARTY_NOTICES.md). No push or publication
is authorized outside the private first-stage boundary.
