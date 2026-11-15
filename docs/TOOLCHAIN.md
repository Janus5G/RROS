# Toolchain and environment inventory

**Inventory date:** 2026-11-08. Versions below were observed in the workspace
environment; they are not claims about other machines.

## Host and WSL

| Component | Observed value | Use |
|---|---|---|
| Host OS | Windows 11, build 26200 | Workspace and orchestration |
| Host Python | 3.14.3 | Gate orchestration |
| WSL distro | Ubuntu 26.04 (resolute), x86-64 | Freestanding builds and guest tests |
| WSL Python | 3.14.4 | Build/test utilities |
| GCC (available fallback) | 15.2.0 | Host-side probes only unless a target is explicitly configured |
| GNU binutils | 2.46 | Inspection utilities |
| QEMU x86-64 | 10.2.1 | UEFI virtual guest |
| OVMF package | 2025.11-3ubuntu7.3 | x86-64 UEFI firmware; `OVMF_CODE_4M.fd` is present |
| QEMU AArch64 | 10.2.1 (`1:10.2.1+ds-1ubuntu3`) | Locally extracted `qemu-system-arm` package; `virt` machine |
| AArch64 UEFI firmware | 2025.11-3ubuntu7.3 | Locally extracted `qemu-efi-aarch64` AAVMF images |
| Physical Pi 5 | Not available for this audit | Hardware remains `HARDWARE_NOT_VERIFIED` |

Windows-native Clang, LLD, QEMU and CMake are not available. WSL package
installation via passwordless sudo is not available. To avoid a system-wide
install, Clang/LLD and ARM QEMU/firmware were downloaded as Ubuntu packages and
extracted under the ignored workspace directory `.toolchain/`.

## Selected freestanding toolchain

- Clang: Ubuntu clang 21.1.8 (`clang-21`, package version
  `1:21.1.8-6ubuntu1`).
- LLD: Ubuntu LLD 21.1.8 (`lld-21`, package version
  `1:21.1.8-6ubuntu1`).
- LLVM runtime packages: `libllvm21`, `libclang-cpp21`, `libclang1-21`,
  `libclang-common-21-dev`, `llvm-21-linker-tools`, same Ubuntu package
  release.
- Packages were extracted locally; they are not project source and are not
  included in release artifacts. Package signatures/repository metadata are
  supplied by the configured Ubuntu archive. Exact package hashes are captured
  by the gate report.
- `scripts/bootstrap-toolchain.sh` documents the rootless extraction. The gate
  probe compiles and links a freestanding x86-64 EFI application and validates
  the PE/COFF EFI subsystem.
- `scripts/bootstrap-aarch64-emu.sh` extracts the ARM emulator, matching QEMU
  modules/data and AAVMF firmware locally. The Ubuntu updates archive returned
  404 for the newest QEMU package revision during setup; the same QEMU 10.2.1
  upstream version from the available `resolute/main` package was extracted
  instead. No system package was installed.

The reference snapshot uses LLVM/Clang and LLD 20.1.8. RROS selects
the installed, compatible 21.1.8 toolchain rather than claiming version
identity with that snapshot. Toolchain changes require re-running the gate
probe and recording new versions and hashes.

## Build strategy

1. Keep tool binaries in `.toolchain/` and build outputs in `build/`; do not
   install build tools globally.
2. Use `scripts/bootstrap-toolchain.sh` once in WSL; it needs the configured
   Ubuntu package archive but does not require sudo.
3. Use Clang/LLD target flags and explicit linker inputs for each EFI/native
   artifact. Do not rely on host CRT, libc or a Linux kernel at target runtime.
4. Use Python for reproducible orchestration and tests; keep the privileged
   kernel freestanding.
5. Run QEMU with OVMF for x86-64 guest tests and QEMU `virt` with AAVMF for
   generic AArch64 tests. Neither ARM emulation nor x86 emulation proves
   Raspberry Pi 5 peripheral behavior.
6. Hash source inputs and output artifacts; never describe an absent or
   structurally invalid artifact as built.

## Dependency/license inventory

| Dependency | Current role | Source/provenance | Redistribution status |
|---|---|---|---|
| Clang/LLD 21.1.8 | Local freestanding compilation/linking | Ubuntu `resolute` packages listed above | Not redistributed; verify notices if bundled later |
| QEMU 10.2.1 | Local x86 software validation | Installed Ubuntu package | Not redistributed |
| OVMF 2025.11 | Local x86 UEFI firmware | Installed Ubuntu package | Not redistributed |
| QEMU 10.2.1 ARM | Generic AArch64 software validation | Ubuntu package `1:10.2.1+ds-1ubuntu3`, locally extracted | Not redistributed |
| QEMU system modules/data | Runtime support for the local ARM emulator | Matching `1:10.2.1+ds-1ubuntu3` Ubuntu packages | Not redistributed |
| AAVMF 2025.11 | Generic AArch64 UEFI firmware | Ubuntu package `2025.11-3ubuntu7.3`, locally extracted | Not redistributed |
| Python 3.14.x | Build/gate scripts | Host and Ubuntu runtimes | Not copied into target |
| RoboForge | Read-only future MCP compatibility reference | `Janus5G/RoboForge` commit `b0dc46f860faedb6f00a36b2c636a2b31c2b290e`; MIT per repository LICENSE | No code copied; repository remains unchanged |
| OSBuild.pdf | Architectural reference | Supplied workspace file | Not a code dependency or source license |

No third-party runtime library or firmware is currently linked into
RROS. Review component-specific license texts and produce notices
before any future redistribution.
