# RoboticRasOS (RROS) architecture

## Product boundary

RoboticRasOS (RROS) is an original native operating system. Linux/Windows may host
development tooling but are not runtime kernels or guest dependencies. The
primary target is Raspberry Pi 5 Model B AArch64; x86-64 UEFI/QEMU is the
software regression platform. A shared kernel/service/SDK design uses distinct
architecture and device backends.

## Layers

1. **Firmware/boot:** x86-64 UEFI application and validated handoff; separately
   documented Raspberry Pi firmware and U-Boot/UEFI chain.
2. **Architecture:** CPU entry, exception vectors, interrupt controllers,
   timers, context and MMU operations in `arch/x86_64` and `arch/aarch64`.
3. **Kernel:** freestanding C17 core, diagnostics, synchronization, memory
   ownership, scheduler, process/syscall boundaries and capability checks.
4. **HAL/drivers:** documented resource discovery, MMIO/DMA ownership, storage,
   network, display, input and platform-specific devices.
5. **Services and ABI:** versioned file, event, graphics, network, AI and MCP
   contracts; only introduce interfaces with explicit bounds and ownership.
6. **Applications/desktop:** protected native applications, common UI controls,
   Classic and Flow shells, language runtimes and browser components.

Privilege separation is an acceptance goal, not an assumption that all drivers
or desktop services are already isolated. AI inference and MCP tools must never
execute in privileged kernel context. Tool/model output is untrusted data.

## Shared contracts and platform separation

Common C interfaces may describe scheduling, memory, events and services, but
must not encode undocumented board registers or assume an x86 memory map.
Firmware records, physical memory, MMIO ranges and DMA buffers are validated
before ownership transfer. Platform implementations are selected explicitly at
build time. `kernel/memory.c` currently provides a bounded page allocator,
owner-checked heap and W^X virtual-range registry over a firmware-backed arena;
the registry does not yet create hardware page-table entries. G04 validates
firmware timer events, architecture-counter progression and UEFI console input
in both emulator guests; native interrupt-controller and input drivers are
not yet implemented.

## Build and artifact policy

Use freestanding Clang/LLD builds, architecture-specific startup and linker
descriptions, Python host scripts and QEMU/OVMF for x86-64 guest execution.
Artifacts are named only after successful build and structural validation.
Every tested artifact is tied to source revision, exact tools, command,
environment and SHA-256. Build success, emulator behavior and physical
hardware behavior remain distinct evidence classes.

Historical binary names and ABI identifiers retain `ExperimentalOS`; the RROS
presentation does not rename or replace accepted artifacts.

## Incremental protection model

The target model is kernel/user privilege separation, per-process address
spaces, validated executable segments, W^X mappings, bounded syscalls,
capability authorization and safe resource retirement. Gates must test each
implemented property; documentation alone is not proof.

G05 now exercises a bounded first implementation: x86-64 CPL3 and AArch64 EL0
guests enter through architecture-specific return frames, dispatch validated
syscalls, switch between two fixed test processes, contain expected isolation
faults and verify cleanup/reaping. The test maps kernel memory supervisor-only
and uses process-specific user stacks. It is cooperative and single-CPU; it
does not yet provide a general executable loader, multicore scheduler or timer
preemption.

G06 adds a shared fixed-capacity file API over 512-byte block callbacks, dual
checksummed metadata snapshots, copy-on-write extents and per-file data checks.
The host test uses a disposable file-backed volume for reopen and interrupted
write recovery; QEMU guests currently exercise only the RAM-backed device.
Neither test establishes a native physical/firmware block-device driver.

G07 adds a bounded Ethernet/ARP/IPv4 protocol core with ICMP, UDP, DHCP, DNS
and TCP handshake handling behind a frame-device interface. Its host and
x86-64/AArch64 guest acceptance tests use a deterministic in-memory two-endpoint
NIC; they do not exercise QEMU's network devices, host interfaces, LAN access,
or physical networking hardware. Network driver and external-connectivity
support therefore remain unverified.

G08 uses UEFI GOP as a temporary test framebuffer and a freestanding software
renderer with clipping, RGB/BGR output, text primitives, layered windows and
pointer-driven activation, dragging and closing. Host tests use a bounded
memory buffer; QEMU guest tests capture the GOP descriptor before
ExitBootServices, then validate framebuffer writes/readback and window input
after handoff. The keyboard trigger is QMP-injected and pointer gestures are
deterministic test events, not a native input driver. This is not yet a
persistent kernel display service or Raspberry Pi display/input driver.

G10 adds a Classic shell over the G08 renderer: taskbar, launcher and Console,
File Manager and Settings windows. The guest-tested app windows own process
and thread lifecycle records with isolated W^X virtual code/data/stack ranges;
close terminates and reaps those records. These windows are lifecycle fixtures,
not native application binaries or a general desktop process launcher.
Framebuffer rendering is tested in x86-64 and AArch64 QEMU; UI actions after
the injected activation key are deterministic test events. Native display and
input drivers and Raspberry Pi hardware remain unverified.

G11 adds Flow as a second shell over the same renderer and application lifecycle
services. Its dock switches existing app windows, and its light/dark palettes
are shared across the shell and app windows. Host/sanitizer and both QEMU
architectures test Flow behavior and rerun Classic lifecycle checks. This is
not yet a native input/display service or execution of the app fixtures.

G12 adds a bounded UEFI configuration-table lookup and FDT v16/v17 metadata
parser with multi-compatible matching, 3-cell PCI `assigned-addresses`
translation that keeps PCI config-space `reg` metadata separate, and
interrupt-parent, clock/reset provider and DMA-range resolution. It decodes
GIC-400 interrupt specifiers, exposes fixed-clock rates and tests a transactional
MMIO mapping lifecycle plus a physical page-pool DMA allocator with explicit
cache-sync callbacks. The GICv2 SPI register/dispatch path now has a
mapper-backed platform lifecycle that maps its distributor and CPU interface
and supports explicit Group-1 start/stop, alongside the AArch64 IRQ vector
entry. Target startup does not yet provide the trusted Pi mapping or activate
the controller. Firmware clock/reset operations are not wired.
A bounded
ECAM function scanner and RP1
identity matcher are also fixture-tested, but do not initialize the BCM2712
PCIe host bridge. The platform provides bounded MMIO interfaces and
register-level RP1 GPIO, PL011 UART, DesignWare I2C and SPI controller paths.
Host register models and an AArch64 guest fixture exercise their state and
error paths; this does not establish real peripheral behavior. The platform
mapping callback must enforce page attributes and hardware permissions, and
the DMA path requires device-specific translation and cache synchronization;
no Pi DMA-capable driver consumes it yet.
Firmware clock/reset operations, PCIe host initialization/MSI-X integration,
and USB, microSD, HDMI, Ethernet, Wi-Fi, audio and camera interfaces remain
incomplete. Generic AArch64 QEMU is not a Pi 5 hardware test.

## Future services

AI Core, MCP, networking, graphics, storage, desktop, browser and language
runtimes are staged behind stable core interfaces. Remote AI providers are
optional and require explicit configuration. MCP starts disabled, local-only
and scoped; cross-device access needs a separate authenticated encrypted
transport and explicit admission policy.

## Raspberry Pi development, packages and deployment

The future native Pi 5 Development & Deployment Manager uses the versioned SDK
and a separate ExperimentalOS MCP compatibility layer. Build and packaging
operations are bounded and profile-based; they do not expose general shell
execution or AI signing/deployment authority. JSON manifests and configuration
are metadata only; executables and resources are separate digest-checked
artifacts. A versioned manifest binds package identity/version, OS/architecture
compatibility, dependencies, capabilities, artifact SHA-256 digests, signature
and signer identity, installation constraints and rollback data. The recipient
verifies schema, trust, signatures, hashes, dependency/compatibility policy,
quotas and direct approval before activation.

Local-network distribution is a separate authenticated, encrypted,
trust-enrolled node-transfer service with scoped capabilities, quota controls,
integrity checks and audit events. RoboForge's optional
`http://127.0.0.1:3001/mcp` gateway remains opt-in and loopback-only; it is not
the node transport and must not be forwarded to the LAN. RoboForge contracts
are implemented only through a separate versioned adapter. MCP outputs remain
untrusted.

Future deployment acceptance uses a three-node simulation to test authorized
and unauthorized senders, malformed metadata, tampering, signatures,
compatibility, dependencies, capability denial, interruption, installation
failure, rollback and successful recipient-approved activation. This does not
verify physical Raspberry Pi hardware.
