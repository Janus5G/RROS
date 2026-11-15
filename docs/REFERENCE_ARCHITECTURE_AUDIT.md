# Reference architecture audit

**Reference:** `OSBuild.pdf`, 34 pages, WOS v0.0.33 (R47), dated 8 November
2026. The supplied PDF and `MASTER_PROMPT.md` were read in full. The PDF text
was extracted from all pages and all pages were visually reviewed, including
the embedded architecture, build, scheduling, SDK, AI, graphics and hardware
diagrams. This audit summarizes the concepts; it does not reproduce WOS source
or claim compatibility with WOS.

## Architectural decisions and acceptance

| PDF section / pages | Reference-derived requirement | ExperimentalOS implementation choice | Acceptance evidence | Difference / status |
|---|---|---|---|---|
| 1. What you are seeing, pp. 3-5 | WOS is an experimental independent OS; native ARM and x86 builds are distinct. Selected third-party components keep their own licenses. | Original kernel and ABI, separate architecture backends, no Linux/Windows runtime dependency. | Guest boot and dependency/provenance checks per target. | No WOS source or binary compatibility is assumed. Not implemented. |
| 2. How technical designs lead development, pp. 6-7 | Keep requirements, source, artifacts and observed behavior distinct; tests and ownership are part of designs. | Versioned gate state, explicit contracts, test artifacts and recorded results. | Gate runner rejects absent tests and preserves failures. | Gate system is being established. |
| 3. Development, testing and deployment, pp. 8-9 | Use isolated VM builds, layered tests, regression evidence and artifact identity; emulation is not physical-hardware evidence. | QEMU/UEFI as x86 regression platform; separate Pi hardware status; SHA-256 manifests. | Reproducible guest tests and separate hardware reports. | No deployment or physical write is part of the software gates. |
| 4. Q&A, pp. 10-11 | No OS-kernel dependency; broad application compatibility and Vulkan are not implied; plans are not features. | Native interfaces only; each capability is reported at its tested level. | ABI, runtime and feature-specific tests. | External application compatibility and Vulkan are not assumed. |
| 5. Cost, p. 12 | Project costs need attribution. | No paid service or model provider is required for boot/build; dependencies are locally inventoried. | Reproducible build inputs and opt-in provider tests. | No budget or external-service authorization is inferred. |
| 6. Architecture overview, p. 13 | Shared application/service logic sits over architecture interfaces; some UI/drivers may remain privileged. | C17 shared kernel and HAL with x86-64/AArch64 implementations; privilege boundaries mature gate by gate. | Cross-architecture builds, ABI and privilege tests. | Do not call the initial design a microkernel or claim unimplemented isolation. |
| 7. Classic, Flow and command shells, pp. 14-15 | Two desktop experiences share core services; privileged terminal commands are distinct from a protected console. | Shared window/UI services and typed command APIs; shells are later gates. | Guest interaction/lifecycle tests and explicit command authorization. | No desktop exists yet. |
| 8. Power-on to desktop, p. 15 | UEFI/firmware handoff, memory map and framebuffer are validated; Pi and VM use different boot chains. | x86-64 UEFI first; Pi firmware/U-Boot path tracked separately; staged boot diagnostics. | Actual QEMU boot, malformed handoff tests, later Pi hardware report. | UEFI details will follow the current available firmware, not assumed WOS internals. |
| 9. Compilers, formats and build pipeline, pp. 16-17 | Freestanding C/assembly, target-specific executable formats and recorded inputs; host Python is build tooling. | C17/assembly, LLVM Clang and LLD; PE/COFF EFI initially, with an explicitly versioned native ABI later. | Compiler/linker smoke tests, PE inspection and reproducible artifact hashes. | Current environment uses locally extracted LLVM 21.1.8, not WOS's stated 20.1.8. |
| 10. One codebase, multiple builds, p. 18 | Shared system/application contracts with separate boot, CPU and device backends. | Common headers and services plus isolated `arch/x86_64` and `arch/aarch64` trees. | Both targets build against common interfaces; platform-specific tests remain separate. | AArch64 remains unverified until its toolchain and emulator tests pass. |
| 11. Memory, process isolation and faults, p. 19 | Validate memory regions, ownership, executable ranges, permissions and syscall buffers; cleanup waits for outstanding users. | Explicit physical-memory map, page ownership, W^X, checked ELF loading and bounded syscalls. | Allocator/page-table tests, malformed image tests and fault cleanup tests. | No protection guarantee until the relevant implementation and tests land. |
| 12. Multitasking and responsiveness, pp. 20-21 | Threads/process states, bounded work and safe resource lifetime; do not claim hard real-time behavior. | Incremental scheduling and cancellation with per-target context-switch code. | Timer, lifecycle, concurrency, latency and resource-retirement tests. | Real-time guarantees are out of scope absent proof. |
| 13. Native SDK, pp. 22-23 | Versioned ABI, ownership, limits, events and service-mediated access; full-frame copies can remain costly. | Stable SDK introduced only with documented structure sizes, errors, buffer caps and ownership. | ABI compatibility, lifecycle and responsiveness tests. | SDK is not yet implemented. |
| 14. Word Search, p. 24 | Example application only. | No kernel dependency or gate requirement. | None. | Illustrative only, as the master specification states. |
| 15. AI Core, pp. 25-26 | AI work runs in protected workers; credentials are separated; model output does not authorize actions. | Optional user/service-context inference jobs, bounded results, cancellation and capability checks. | Actual local inference where runtime permits; isolation and cancellation tests. | No model/provider is required for boot; no autonomous privileged agent. |
| 16. Languages and tools, p. 27 | Interpreter ports require OS adapters; CPython/QuickJS do not imply arbitrary package compatibility. | Investigate runtimes after core ABI and file/time/I/O contracts stabilize. | Per-runtime execution and module compatibility tests. | No language compatibility claims before evidence. |
| 17. Graphics, p. 28-29 | CPU rendering and GPU transport are separate; Vulkan needs an actual backend. | Start with UEFI framebuffer/software rendering; investigate GPU paths after correctness and measurements. | Interactive guest tests, resource lifecycle and fixed-workload benchmarks. | No GPU acceleration or Vulkan claim from an abstraction alone. |
| 18. Browser, p. 29 | Isolate document/JS/image/network work and measure full loading, responsiveness and cleanup. | Optional browser service after networking, process and graphics interfaces exist. | Controlled page fixtures and complete loading/input/cleanup metrics. | No Chromium compatibility claim. |
| 19. Hardware discovery and services, pp. 30-31 | Validate firmware/device descriptions, MMIO/DMA and the whole device workflow. | Separate documented platform drivers; simulation, emulator and physical evidence are reported separately. | Driver fixtures plus appropriate emulator and physical tests. | No Pi peripheral is claimed operational based only on emulation. |
| 20. Debug cable/workflow, pp. 32-33 | Serial is a diagnostic/recovery interface; physical installation and port ownership require coordination. | Serial-first diagnostics; no flashing or persistent-media writes without separate authorization. | Captured guest logs; physical procedures documented before hardware activity. | No physical cable/device access is available in this environment. |
| 21. Storage/network/audio/shared services, p. 33 | RAM and persistent storage differ; network and printing claims require end-to-end checks. | Disposable VM media and controlled network tests only; services are incremental. | Filesystem recovery, network stack and service-specific guest tests. | Physical storage, printing and audio remain unverified. |
| 22. Security and roles, p. 34 | Existing protections are not certification; authorization belongs in trusted services; AI output stays data. | Capability checks, bounded parsing, privilege separation and auditability are explicit contracts. | Security regressions, malformed-input tests and independent review. | No certification or complete account/role system is claimed. |

## RoboForge compatibility source inspection

Read-only inspection was performed against `Janus5G/RoboForge` at commit
`b0dc46f860faedb6f00a36b2c636a2b31c2b290e`; the repository `package.json`
reports version 0.4.1 and the license is MIT. The retrieved MCP gateway source
defines three tools, accepts loopback-only requests, checks host/origin, limits
requests/replies and has no shell or generic plugin execution. It is disabled
unless `ROBOFORGE_MCP_ENABLED=1`. The supported protocol versions in the
source include `2025-03-26`, `2025-06-18` and `2025-11-25`. The handover
document calls itself v0.4.0, so that document is not treated as the current
repository version.

The catalog and module-status tools are read-only metadata; the Refract tool is
separately opt-in, bounded and only produces text. Managed grants require an
explicit signed, one-use authorization for controlled calls. The README,
gateway, managed-grants implementation, handover, fair-use compatibility
document, tests and smoke test were inspected. The smoke-test contract expects
three tools and ten registered modules, but this is not an end-to-end
ExperimentalOS integration test.

RoboForge and other Janus5G repositories remain unchanged. No MCP endpoint was
started or contacted. Any future adapter must retest the actual checked-out
contract, remain disabled by default, reject remote access by default, and
never pass tool output to privileged execution.

## Licensing and provenance

- `OSBuild.pdf` is used as an architectural reference, not as a source-code
  license. ExperimentalOS implementation is independent.
- The local Clang/LLD packages and system QEMU/OVMF are development tools and
  are not copied into release images. Their package provenance is recorded in
  [`TOOLCHAIN.md`](TOOLCHAIN.md); licenses/notices must be audited before
  redistribution.
- RoboForge's MIT license applies to RoboForge, not to ExperimentalOS. No
  RoboForge source is copied into this project.
- No project-wide license is selected yet. Review future vendored libraries,
  firmware and generated assets before selecting a license or assembling an
  image.

## Audit limits

This is an architecture and provenance audit, not a claim that the features
described by either document exist in ExperimentalOS. External repositories
were not cloned or modified. Hardware, remote provider and production
integration behavior remain unverified.
