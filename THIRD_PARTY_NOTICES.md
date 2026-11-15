# License and third-party notices

## Project license

Original, assessed RROS software is licensed **GPL-3.0-or-later**, following
the owner's direction. See [LICENSE](LICENSE), the exact
[owned-file manifest](docs/license-owned-files.json) and the
[component inventory](docs/LICENSE_INVENTORY.md). This supersedes the old
MIT preference; it does not relicense third-party or unassessed material.
GPL rights are not restricted by private repository visibility.
Project publication approval remains a separate decision.

## Referenced and provisioned components

| Component | Provenance / use | Release treatment |
|---|---|---|
| LLVM/Clang/LLD 21.1.8 | LLVM toolchain; upstream Apache-2.0 with LLVM exceptions; consult packaged notices | Build-time only, not bundled in public archives |
| QEMU 10.2.1 | Emulator; GPL and component-specific terms | Test-time only, not bundled |
| OVMF/AAVMF / EDK2 | UEFI test firmware; BSD-2-Clause-Patent and component-specific terms | Test-time only, not bundled |
| Pi UEFI v0.3 | [worproject/rpi5-uefi](https://github.com/worproject/rpi5-uefi), archive/hash in [pins](scripts/pi5_hw/boot_chain_pins.json) | Independently fetched; firmware/config/DTB excluded from public probe archive |
| TF-A, edk2-non-osi, Broadcom firmware/DTB | Components of the Pi firmware chain with distinct terms | Complete component-by-component redistribution review remains unresolved |
| pyserial 3.5 | Serial-capture host dependency; BSD terms upstream | Dependency only; not vendored |
| actionlint 1.7.7 | MIT-licensed Actions validator, archive SHA-256 pinned in [bootstrap](scripts/release/bootstrap-actionlint.sh) | Test-time only; not bundled |
| RoboForge | Currently PRIVATE; reference revision `b0dc46f860faedb6f00a36b2c636a2b31c2b290e`; [G15 provenance](docs/G15_PROGRESS.md) | Owner-authorized local read-only checkout; no upstream tree/assets bundled; CI cross-repository access unavailable |
| ChromaNeural | Currently PRIVATE; revision `3ce209ccd2aeb52094fbaade4429632eb17d3340`; historical Apache-2.0 plus component terms; [G16 provenance](docs/G16_PROGRESS.md) | Local read-only reference; no upstream tree/banner/screenshot bundled; CI cross-repository access unavailable |
| PRISME | G16 records **All rights reserved** | Protocol references only; no proprietary source or visual asset bundled |
| ChromaPlex references | G16 records exact accessible/inaccessible repositories and pending adapters | No claim that missing dependencies are operational |
| WOS article / OSBuild.pdf | Historical architectural reference, not RROS source or demonstration | Removed from current branch and all new packages; old Git-history copy remains uncleared; history not rewritten |

This summary is **not blanket third-party clearance**. Captured upstream
fixtures and protocol vectors retain their separate provenance treatment. Check the actual
license/notices for each pinned component, including transitive firmware
components; a GitHub repository-level license label is insufficient.

## Visual provenance

The [hero banner](docs/assets/rros-banner.svg) and
[English](docs/assets/rros-architecture.svg) /
[Danish](docs/assets/rros-architecture.da.svg) architecture illustrations are
original repository artwork, not runtime screenshots or hardware photographs.
The [Danish hero](docs/assets/rros-banner.da.svg) and bilingual README badges
are also original artwork under GPL-3.0-or-later, not physical or runtime
evidence. No third-party fonts or images are shipped. Unpublished desktop
concepts and owner-document packages are outside this presentation repair
and its published owned-file manifest.
No verified desktop capture or physical demonstration media is currently
available. WOS and unrelated ecosystem assets are not reused.

Repository-local CI badges are verification snapshots linked to exact private
Actions runs, not live public status endpoints. The software-check badge records
seven passing Code Review checks in run `43456276606` at `e3ff9e6`; the ARM64
badge records four passing host suites in run `43457450774` at `97f39d9`.
Documentation, target, stage and license badges describe the repository's
documented state, not hardware certification or a license grant.

Future tester photos or videos must be genuine, clearly identified and supplied
with permission for the intended use.
