# Component license and redistribution inventory

Assessment: 2026-11-13. This is a file-scoped implementation of the owner's
GPL direction, not a blanket relicensing of every file in the repository.

## Original RROS software

Project-owned kernel, drivers, architecture/boot code, SDK source, host tools,
test code and original SVG/site assets listed in
[license-owned-files.json](license-owned-files.json) use **GPL-3.0-or-later**.
The owner licenses these assessed files through this explicit manifest and
the repository LICENSE. Published Danish README artwork and license badges
also carry SPDX/copyright notices. Older covered source files do not all
carry file-level notices; this presentation repair does not modify OS source.
The manifest lists published files only, not unpublished local work. New
community guide, BOM, PDF generator and offline collector are original
GPL-covered project material. Filtered source downloads further exclude
uncleared fixtures and reference material; see their SOURCE_PROVENANCE.json.
The unchanged official [GPLv3 text](../LICENSE) was downloaded from
<https://www.gnu.org/licenses/gpl-3.0.txt>; SHA-256:
`3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986`.

Ownership basis: the owner's direction calls for original implementation;
[reference audit](REFERENCE_ARCHITECTURE_AUDIT.md) records independent choices;
[G15](G15_PROGRESS.md) and [G16](G16_PROGRESS.md) distinguish original adapters
from upstream execution/data. The source history introduces the small UEFI
definitions at `c26dd56`, SHA-256 at `3501805`, and ChaCha20 at `fae8a7f`.
The tracked source/asset scan found no incorporated upstream source tree or
pre-existing third-party copyright headers in the covered files. Algorithm,
ABI and protocol implementations are original code, not a license grant over
their specifications. This evidence cannot independently prove authorship;
any newly identified copied source must be removed from the owned manifest,
retain its original notices, and receive a separate compatibility review.

Excluded: `OSBuild.pdf`, historical build binaries, external checkouts,
toolchains, firmware/DTB, captured upstream JSON and upstream-derived vector
files. No GPL header is applied to these. Unpublished owner workbooks,
desktop concepts are not part of this published manifest. Historical/reference documents are not newly
licensed by implication.

Redistributing GPL-covered source requires retaining notices/license and
documenting modifications. Redistributing corresponding object code requires
GPLv3-compliant Corresponding Source and installation information where
applicable; a binary plus LICENSE alone is not sufficient. Private visibility
does not remove recipients' GPL rights. Public publication approval remains
a separate project decision, not an additional restriction on the GPL grant.

## Incorporated / referenced code and data

| Component | Exact source / revision | License / obligations | Bundled / disposition |
|---|---|---|---|
| RROS original source | File-scoped manifest; owner GPL direction; Git history | GPL-3.0-or-later; retain notices, provide Corresponding Source for binary distribution | Covered repository files; historical binaries not newly cleared by this repair |
| RoboForge captured JSON fixtures | `Janus5G/RoboForge` `b0dc46f860faedb6f00a36b2c636a2b31c2b290e`; `tests/gates/fixtures/roboforge/` | MIT recorded in G16; exact captured-data copyright/notice coverage not established | Existing fixtures preserved for CR/gate evidence; excluded from GPL manifest; public source clearance blocked until upstream notices/ownership confirmed |
| ChromaNeural digest vectors | `Janus5G/ChromaNeural` `3ce209ccd2aeb52094fbaade4429632eb17d3340`; `tests/gates/fixtures/chromaneural/vectors.txt` | Apache-2.0 recorded; derived by executing upstream, not copied implementation. Exact data-rights treatment unconfirmed | Historical fixture retained; excluded from GPL manifest; no blanket GPL grant |
| Original parser fixtures | `tests/pi5_hw_validation/fixtures/`; original synthetic records and project-curated factual QEMU marker excerpts | GPL-3.0-or-later for original curation; not firmware source or physical evidence | Included for CR/parser regressions; provenance remains in fixture text |
| RROS MCP client/server/adapters | `kernel/mcp_*`, original bounded C implementations; G14–G16 | GPL-3.0-or-later | RROS code; no third-party MCP SDK linked or vendored |
| RoboForge server / MCP dependencies | Same pinned private checkout; Node backend, used locally read-only | MIT project-level record does not clear all transitive packages; upstream lockfile/license review required before redistribution | Not bundled; CI cross-repository execution unavailable |
| ChromaNeural client/server | Same pinned private checkout | Apache-2.0 plus component-specific terms | Not bundled; no live runtime integration claim |
| PRISME | `Janus5G/PRISME` `2270d347e93a3863582362474ddc333a557a90a6` | All rights reserved; **no source, vectors or text copied** | Reference only; no redistribution authorized |
| ChromaPlex-related repositories | `Janus5G/chromaplex-os` `9ec06fba945f443ab57f25d2507fda42c02e1a4c`, others unresolved | MIT where recorded; no exact revision/clearance for unavailable adapters | Not bundled or operationally integrated |
| RFC 4231 / RFC 8032 numeric vectors | Standard test values in original C/tests and generator | Facts/test values are not an incorporated cryptography library; document provenance | Original implementations; no RFC document bundled |
| WOS article / OSBuild.pdf | Source URL in master plan; historical supplied PDF | Rights/redistribution permission unestablished | Deleted from current branch; historical blob remains in Git history, which is not rewritten; excluded from every new package |

## Host tools and dependencies — separate processes, not linked OS libraries

Versions are reproducibly selected in
[provision-ci.sh](../scripts/release/provision-ci.sh) at Ubuntu snapshot
`20261113T000000Z`. Every CI run retains `.toolchain/ci-packages.txt` with the
actual installed transitive versions. No host executable, library or package is
newly redistributed by this repair or linked as a RROS runtime dependency.

| Component | Version/source | License / redistribution requirements | Treatment |
|---|---|---|---|
| Clang | LLVM 21.1.8, Ubuntu `1:21.1.8-6ubuntu1` | Apache-2.0 WITH LLVM-exception and component notices; retain them if redistributing | Build only |
| LLD | Same LLVM release/package revision | Apache-2.0 WITH LLVM-exception; retain component notices | Build only |
| LLVM supporting libraries | Same release, Ubuntu package set in toolchain inventory | Same umbrella, per-file exceptions/licenses must be retained | Build only |
| GCC / compiler runtime | Ubuntu snapshot package/version captured in CI | GPL-3.0-or-later and GCC-runtime-exception where applicable; no assumption that arbitrary GPL compiler code can be linked | Host test compiler only |
| GNU Make | Snapshot-selected version in CI | GPL-3.0-or-later; retain source obligations if bundled | Build only |
| Python / standard library | Owner host 3.14; CI interpreter/package version recorded per run; CPython distributions | PSF-2.0 and included historical/component terms | Installed separately on development/test hosts |
| pyserial | `pyserial/pyserial` tag `v3.5`; hash-pinned wheel/sdist in requirements | Verified BSD-3-Clause LICENSE.txt; preserve Chris Liechti copyright, conditions/disclaimer; no endorsement | Installed separately by tester, not vendored |
| QEMU | 10.2.1, Ubuntu `1:10.2.1+ds-1ubuntu3.3` | GPL-2.0-or-later umbrella plus per-component licenses; copies require source/notices review | Separate software test process, not bundled |
| xorriso / libisoburn | Snapshot-selected CI package | GPL-2.0-or-later umbrella; dependent libraries have separate terms | Build only, not bundled |
| Git | Snapshot-selected CI package | GPL-2.0-only umbrella; separate process, not incorporated | Not bundled |
| curl / libcurl | Snapshot-selected CI package | curl license and dependent TLS/library terms | Download process only |
| GitHub CLI | `2.46.0-4` Ubuntu package | MIT plus dependency notices | CI/owner host only |
| Node.js | `22.22.1+dfsg+~cs22.19.15-1ubuntu1` CI package | MIT plus V8/OpenSSL/component notices | Optional upstream host test process, not OS runtime |
| actionlint | `rhysd/actionlint` 1.7.7, pinned archive SHA in bootstrap | MIT; retain copyright/license if bundled | Validation only |
| pypdf | PyPI 6.1.3, exact wheel SHA-256 in tests/community-requirements.txt | BSD-3-Clause per published package metadata | Independent validation of generated PDFs only; separately installed, never bundled |
| checkout action | `actions/checkout` `3d3c42e5aac5ba805825da76410c181273ba90b1` (7.0.1) | MIT plus runtime/dependency notices | GitHub-hosted Node24 action, not in archives |
| upload-artifact action | `actions/upload-artifact` `cf430e030ddbb5b0abf93d22962f4752f3646cd9` (7.0.2) | MIT plus runtime/dependency notices | GitHub-hosted Node24 action, not in archives |
| Ubuntu container / CA bundle / transitive packages | Digest in release workflow; complete run-specific dpkg list | Distribution is an aggregation, not a single license. Exact package copyright files govern | CI only; no container redistribution |

Unpinned transitive package versions are not invented here: the immutable
snapshot and actual dpkg evidence are the source of truth. Bundling any host
tool requires a new per-package/per-file audit and retention of its real
upstream notices. No required third-party header has been replaced.

## Firmware chain — separate third-party terms

Pi UEFI v0.3 release tag resolves to commit
`c1ca184c608dca75a346cc56b8eaf42648d83e86`; tag object
`514cf42055941a0f4dc845d1073f287d71d78f2e`. Source:
<https://github.com/worproject/rpi5-uefi>.
Archive/per-file size and SHA-256 are in
[boot_chain_pins.json](../scripts/pi5_hw/boot_chain_pins.json).
The source tag does not establish a reproducible binary-to-source mapping or
complete license manifest for `RPI_EFI.fd` or its DTB.

| Component | Exact upstream pin | License evidence / blocker | Treatment |
|---|---|---|---|
| TF-A fork | `worproject/arm-trusted-firmware` `682607fbd775e37fb5631508434dab9e60220c9a` | Verified `docs/license.rst`: BSD-3-Clause main; FreeBSD/SCC BSD, libfdt GPL-2.0-or-later OR BSD-2-Clause, compiler-rt NCSA OR MIT, Zlib, some Linux bindings GPL-2.0-only OR MIT. Actual binary composition/notice bundle not established | Separate tester download via firmware release; no redistribution by RROS |
| EDK2 fork | `worproject/edk2` `eca8aaebb3ec6e4707a8e5765dd372feb5bbee6e` | Verified top-level BSD-2-Clause-Patent; modules retain individual licenses | Firmware source dependency, not RROS code |
| EDK2 platforms fork | `worproject/edk2-platforms` `8e1779b538bcc1e6dc68d7df625394f933651d7a` | Verified top-level BSD-2-Clause-Patent; platform/module terms separate | Not bundled |
| edk2-non-osi | `tianocore/edk2-non-osi` `1f4d7849f2344aa770f4de5224188654ae5b0e50` | Verified README: no top-level license; nearest module License.txt governs. Selected binary modules/notice coverage unresolved | **Redistribution blocked**, no binary in tester/release archives |
| Broadcom firmware / `RPI_EFI.fd` composition | Pi UEFI v0.3 archive, exact hash in pins | Proprietary/module terms and complete linked binary composition unverified | Owner/tester fetches independently after reviewing terms; no RROS redistribution clearance |
| `bcm2712-rpi-5-b.dtb` | Same archive, exact hash in pins | Exact DTS source/version and selected license not established; do not assume GPL-2.0-only or permissive | **Redistribution blocked** pending provenance and applicable terms |
| `config.txt` | Same archive, exact hash in pins | Release source/config origin known; complete archive's terms not inferred from configuration text | Separate firmware preparation only |
| OVMF / AAVMF | Ubuntu EDK2 package `2025.11-3ubuntu7.3` | BSD-2-Clause-Patent umbrella plus firmware/module terms; retain packaged notices for any future copy | QEMU test firmware only; not bundled |
| Raspberry Pi EEPROM / bootloader | Tester supplies exact `vcgencmd bootloader_version` and firmware identity | Existing device firmware; revision/terms vary, no updater or blob distributed | Not bundled; upgrades require tester's separate decision |
| Wi-Fi / Bluetooth firmware | No trusted/pinned bundle selected | Exact blobs/versions/licenses unavailable | Not bundled; native operation NOT_TESTABLE |

The legacy probe builder stages a **local diagnostic card**, including locally
downloaded firmware. That ignored staging directory is not a distributable
release artifact. Release packaging excludes firmware/DTB/config. This README
repair uploads no firmware, DTB, diagnostic card or binary release package.
Do not upload the legacy card directory.

## Compatibility rules and unresolved blockers

- **GPL-2.0-only cannot be combined into a GPLv3-only covered work.** A separately
  executed Git tool or separate firmware image is not automatically a linked
  derivative. Assess the real integration; do not infer compatibility from
  mere coexistence. Dual GPL-2.0-only OR MIT files may use MIT when verified.
- **Apache-2.0 is compatible with GPLv3**, not GPLv2-only. Preserve Apache
  copyright, NOTICE, modification and patent conditions. Do not erase them.
- BSD/MIT/Zlib components also retain their own required notices; a GPL project
  label never relicenses them.
- Exact edk2-non-osi/Broadcom binary composition, DTB source/license, supplied
  PDF rights and captured upstream fixture notice/data-rights coverage remain
  public-distribution blockers. They are excluded from the GPL manifest;
  their presence in the repository does not imply redistribution clearance.
- No font files or third-party images are shipped. SVG/site fonts are system
  font preferences, not font redistribution. Original bilingual banner,
  architecture and badge artwork listed in the manifest is GPL-covered
  original artwork, not runtime screenshots or hardware photographs.
  Unpublished concepts and owner documents are outside this repair.
  Future tester photographs require separate permission.
