# G12 Raspberry Pi 5 driver expansion progress

Historical private logs referenced below are not included in this distribution.

- Gate status: `SOFTWARE_PASS` (static, driver-fixture and emulator checks; reopened by the owner for implementation completion, work in progress)
- Hardware status: `HARDWARE_NOT_VERIFIED`

## Reopened implementation increments

1. `kernel/pi5_dwc3_core.c`: DWC3 host-mode global core init ported from raspberrypi/linux `31a3488` (`dwc3_core_init`, `dwc3_set_prtcap`): SUSPHY enable, soft reset with version-specific polling, GCTL/GUCTL handling, PRTCAPDIR with read-back, register snapshot rollback on any failure. Tested by `tests/gates/G12_dwc3_core_test.c` (plain and ASan/UBSan) through a register model only. Not operational: no USBH clock provider (IDs 25-28), so startup remains fail-closed; GUID write and quirk properties are intentionally omitted. Hardware: not verified.
2. DRD event buffer lifecycle (`os_pi5_dwc3_core_event_buffer_setup/cleanup`, from `dwc3_event_buffers_setup/cleanup`): 4096-byte, 16-byte-aligned buffer programmed with read-back and rollback; host-only cores allocate none; cleanup refused unless DSTS.DEVCTRLHLT; `host_release` refused while programmed. Covered by the standalone core test (plain and ASan/UBSan).
3. `kernel/pi5_usb_host.c`: production startup/teardown composition (DWC3 core init, DRD event buffer, MSI-X dispatch state, xHCI publish, start, SUSPHY plat start) with a real event-lifecycle check and an interrupt entry point, linked into both kernel builds. `os_pi5_usb_host_self_test` (in the QEMU guest and host runs) injects an MMIO write failure at every write position for DRD and host-only cores, and checks rollback, DMA balance, retryable shutdown and fail-closed platform checks. A core that finished init keeps its configuration after a later-stage failure (no register writes in release, as in Linux). Register-model fixtures only; the platform `integrated_usb_clock_verified` has no provider so physical USB remains fail-closed. Hardware: not verified.

Status reconciliation: G12 stays `SOFTWARE_PASS` only for the previously accepted fixture baseline. It is NOT a production-complete gate: Ethernet/GEM startup, HDMI/HVS modes, camera/CFE data path, audio/I2S path and wireless remain non-operational (see blockers below), and no driver is physically verified.

## Capability state at G12 acceptance

Nothing below is operational on physical hardware; all results are host fixtures, ASan/UBSan and QEMU only.

| Area | Software state | Operational? |
|---|---|---|
| Firmware/device discovery | FDT/UEFI discovery, resource parsing, fixtures PASS | Fixture-verified only |
| RP1 peripherals, GPIO/I2C/SPI/UART | Resource, mapping, IRQ lifecycles, fixtures PASS | No (hardware unverified) |
| USB | xHCI contexts, HID, BOT/SCSI fixtures | No: DWC3 startup disabled; USBH clock IDs 25-28 have no provider descriptors |
| microSD | SDHCI IRQ/PIO fixtures | No (hardware unverified) |
| HDMI | EDID/CTA, mode selection, clock/HVS/DVP ownership fixtures | No: HVS/HDMI disabled in pinned DTS; no mode programming |
| Ethernet | GEM, PHY, DHCP fixtures | No: no external power feedback; no production startup caller |
| Wireless | SDIO2 CMD5/52/53 fixtures | No: no trusted firmware; no WLAN lifecycle |
| Audio | DMA submit/stop, Audio Out stream start/stop fixtures | No: no I2S nodes in pinned DTS; no production caller |
| Camera | CFE mapping and read-only identity probe | No: no CSI-2/FE enable or data path |

Remaining items (see `logs/G12_RESULT.json` remaining_software_work) depend on hardware or unavailable external data and stay fail-closed.

- Latest increments:
  - Added `os_pi5_rp1_cfe_identity_read`, a read-only camera front-end probe
    over the owned CFE mapping: PiSP FE version/status and CSI-2 status
    (register offsets from pinned rp1_cfe `pisp_fe.c`/`csi2.c`). Rejects a zero
    or all-ones version; reports idle from FE_STATUS QUEUED/WAITING/ACTIVE. No
    register writes; no CSI-2/FE/DPHY enable, no camera data path, no hardware
    verification. Fixture in `test_rp1_cfe_mapping`.
    Evidence: `logs/G12_RP1_CFE_IDENTITY_RESULT.md`.
  - Added `os_pi5_rp1_audio_out_stream_start` / `_stream_stop`, composing the
    RP1 DMA channel submit/stop primitives with the Audio Out start/stop
    lifecycle: DMA is submitted first and rolled back if the peripheral start
    fails; stop is accepted only after DMA completion. Fixture-only (extended
    `rp1_audio_out_runtime_bind_self_test`); no production caller, no
    mid-stream abort path, no hardware verification, audio stays inactive.
    Evidence: `logs/G12_RP1_AUDIO_STREAM_RESULT.md`.
  - Added RP1 AXI-DMAC enable/disable and channel submit/stop primitives
    (`os_pi5_rp1_dma_controller_enable/disable`, `os_pi5_rp1_dma_channel_submit/stop`),
    the missing software dependency for DMA consumers. Fixture-only; no physical
    transfer, no audio/GEM activation. Caller contract: enable after IRQ bind,
    disable before unbind. Deviations from Linux: write-enable-only CHEN writes,
    channel-index priority from the pinned DTS, LMS 0, INTSIGNAL excludes ABORTED.
    Evidence: `logs/G12_RP1_DMA_CHANNEL_SUBMIT_*`.
  - At source checkpoint `c5b96b544b13f6fceeea5ae7ba359822db248480`, added
    `os_pi5_hdmi_select_mode`, a pure software display-mode selector over the
    already-filtered progressive CTA modes. It rejects modes above a caller
    pixel-clock limit, prefers the largest active area, then the highest
    refresh (exact cross-multiplied clock/total-pixels comparison), then the
    lowest VIC, and zeroes its output on failure. Fixtures cover the
    148.5/74.25/27/25.175 MHz limits, below-minimum, zero-limit, empty,
    null, oversize and null-output rejections. It performs no register or
    mailbox access and does not program or activate HDMI/HVS (still disabled
    in the pinned DTS). Fixture evidence only; no physical display behavior is
    claimed. See the
    HDMI mode selection result.
  - At source checkpoint `f9afbe7818d50cae461d1804eac1c582d7f289c5`,
    resolved a failed PHY-status fixture by restoring the pinned PHY-ID
    validity contract: an all-zero identifier is accepted, while identifiers
    whose low 29 bits are all ones are rejected. The failed groups exposed an
    attempted zero-ID rejection that contradicted the already-recorded Linux
    rule; no test was weakened. PHY power-verification fixtures now use an
    explicit nonzero PHY ID, while the direct-read and callback fixtures retain
    all-zero acceptance coverage. Focused host/ASan/UBSan passed; formal G12
    exited 0 and remains `IN_PROGRESS`; required G03 then G05 regressions passed
    serially. This is register-model evidence only; no PHY link, Ethernet
    operation, or physical Pi 5 behavior is claimed. See the
    PHY-ID compatibility result.
  - At source checkpoint `e452f7c44d5960137fbe8b6c44ddf1707dcb3a14`,
    strengthened the actual GEM controller-start boundary against unavailable
    external power feedback. The fixture removes the required power verifier
    after queue bases are published, calls controller start, and verifies
    `MISSING_POWER`, all other five prerequisite callbacks still execute, the
    controller remains inactive/unlocked, the IRQ remains disabled, and the
    network-control, interrupt-enable and queue-base registers are unchanged.
    Focused host/ASan/UBSan, formal G12 (exit 0; `IN_PROGRESS`), and serial
    G03 then G05 regressions passed. This proves fail-closed software behavior
    only; it does not provide external rail-power feedback or authorize GEM
    activation. See the
    GEM startup power guard result.
  - At source checkpoint `948f64059e733dc5a4b3d11584b41708ab5e3f0f`,
    added `os_pi5_rp1_gem_phy_reset_startup_check_bind` to connect the
    existing owner-checked PHY-reset verifier to the GEM fail-closed startup
    prerequisite contract. The binder changes only the reset callback and its
    context. Fixtures prove that verification fails before the bounded reset
    operation, succeeds after it, and null inputs are rejected. Focused
    host/ASan/UBSan, formal G12 (exit 0; `IN_PROGRESS`), and serial G03 then
    G05 regressions passed. The binder does not run the reset operation or
    create a production GEM startup caller; external power feedback, complete
    production DMA/cache and interrupt readiness, controller operation and
    physical Ethernet remain incomplete or unverified. See the
    GEM PHY-reset startup binding result.
  - At source checkpoint `19ae9283f9a308777b5d52fbdc188ed63fbf0323`,
    added bounded SuperSpeed isochronous endpoint descriptor validation and
    inactive xHCI endpoint-context staging. The companion Mult and
    `wBytesPerInterval` are range-checked; staged contexts set the isochronous
    direction/type, interval, Max Burst, Max ESIT Payload and zero CErr.
    Fixtures reject reserved/invalid endpoint attributes, invalid intervals,
    Mult values and payload bounds. Focused host/ASan/UBSan, formal G12
    (exit 0; `IN_PROGRESS`), and serial G03 then G05 regressions passed. The
    first formal attempt exposed test-only AArch64 `__chkstk` references; the
    added large configuration fixture was moved to static storage and the
    extra DMA fixture slot was scoped without enlarging the allocator model.
    This increment does not implement isochronous TRB scheduling, streams or
    live USB; controller activation and physical Pi verification remain
    unclaimed. See the
    SuperSpeed isochronous context result.
  - At source checkpoint `093dd7217efeb59210aac4143c8b34680f9cfc11`,
    added an explicit binder for the owner-bound GEM clock lease to the
    existing fail-closed startup prerequisite checks. It installs only the
    clock callback/context and rejects null inputs or a controller different
    from the lease's bound controller. Fixtures verify binding against a real
    software lease and callback dispatch through startup preflight; an
    unavailable lease reports `MISSING_CLOCK`, still runs the other five
    prerequisite checks, and does not enable the IRQ or write its enable
    register. Focused ASan/UBSan, formal G12 (exit 0; `IN_PROGRESS`), and
    serial G03 then G05 regressions passed. The binder does not create a
    production GEM startup caller: power feedback, reset/PHY orchestration,
    production DMA/cache and interrupt readiness, controller operation and
    physical Ethernet remain incomplete or unverified. See
    `logs/G12_RP1_GEM_STARTUP_CLOCK_BINDING_RESULT.md`.
  - At source checkpoint `e182463cc781a95631dcf7f52a561e0d77675b3d`,
    added an owner-bound GEM ETH/ETH-TSU clock-gate lease. Resource attachment
    captures source-validated rates when those gates are disabled without
    changing the existing read-only clock API. Lease acquisition changes only
    the documented enable bit, preserves and revalidates parent, divider,
    selector and rate state, and distinguishes owned from borrowed gates. It
    retains a GEM resource-lifecycle reference and is bound to the exact
    controller object; verification requires that controller to be idle, and
    release restores only gates owned by the lease after the controller is
    quiescent. Fixtures cover initially-disabled enable/restore,
    already-enabled borrowed gates, configuration drift, active-controller
    release rejection, cleanup exclusion and external changes to borrowed
    gates. Focused ASan/UBSan, formal G12 (exit 0; `IN_PROGRESS`), and serial
    G03 then G05 regressions passed. No production GEM caller is connected,
    and power, reset/PHY, DMA/IRQ integration, controller operation and
    physical Ethernet remain incomplete or unverified. See
    `logs/G12_RP1_GEM_CLOCK_LEASE_RESULT.md`.
  - At source checkpoint `9947110516baccdbf309d569c4393c19dd0bd13e`,
    serialized xHCI command-ring access with DMA-ring teardown. A reentrant
    sync-callback fixture reproduced the gap: release could observe no pending
    command and free the ring while command publication was still synchronizing
    its TRB. Command enqueue, completion and completion-event consumption now
    use a separate atomic access claim; teardown claims lifecycle state and
    refuses to free while command access is active. The command claim allows
    completion handling during event dispatch while excluding allocation and
    release. Focused host ASan/UBSan, formal G12 (exit 0; `IN_PROGRESS`), and
    serial G03 then G05 regressions passed on the source checkpoint. This is
    software-only evidence: production DWC3 event/doorbell delivery remains
    inactive, G13 remains `NOT_STARTED`, and Pi 5 hardware remains
    `HARDWARE_NOT_VERIFIED`. See
    `logs/G12_RP1_XHCI_COMMAND_LIFECYCLE_RESULT.md`.
  - At source checkpoint `2e855e9`, extended the xHCI ring lifecycle
    exclusion to allocation, dispatch, and release. Allocation now atomically
    claims the lifecycle before allocating and initializing DMA buffers, and
    rolls back under that claim; dispatch and teardown cannot enter until
    allocation publishes ready state. A DMA allocator callback regression
    attempts release during allocation and verifies it is rejected. Another
    fixture attempts allocation during teardown and verifies the shared
    lifecycle remains claimed until cleanup completes. Focused host
    ASan/UBSan, formal G12 (exit 0; `IN_PROGRESS`), and serial G03 then G05
    regressions passed on the committed source. This remains software-only
    evidence: DWC3 host mode is inactive, USBH clock IDs 25-28 remain
    unsupported, G13 remains `NOT_STARTED`, and Pi 5 hardware remains
    `HARDWARE_NOT_VERIFIED`. See
    `logs/G12_RP1_XHCI_RING_LIFECYCLE_RESULT.md`.
  - At source checkpoint `1f8634e`, serialized xHCI event-ring dispatch with
    ring teardown. A shared atomic state now excludes concurrent/reentrant
    dispatch and ring release; dispatch contention returns `MORE` without
    consuming events, and the bounded USB storage pump retries that condition.
    A reentrant fixture verifies nested dispatch consumes zero events and
    teardown is rejected while the outer callback owns dispatch, after which
    the outer event is acknowledged and teardown succeeds. Focused host
    ASan/UBSan, formal G12 (exit 0; `IN_PROGRESS`), and serial G03 then G05
    regressions passed on the committed source. This remains software fixture
    evidence: production DWC3 event/doorbell delivery is not active, USBH
    clock IDs 25-28 remain unsupported, G13 remains `NOT_STARTED`, and Pi 5
    hardware remains `HARDWARE_NOT_VERIFIED`. See
    `logs/G12_RP1_XHCI_EVENT_LIFECYCLE_RESULT.md`.
  - At source checkpoint `dca9515`, serialized BCM2712 DVP quiescing with
    firmware-clock lease acquisition. A new competing-owner fixture first
    reproduced the race: the quiescence callback could acquire the clock-owner
    field after the old precheck, while DVP reset/gate writes continued.
    Quiescing now atomically reserves that shared owner field for the complete
    transition, revalidates the DVP owner and port mask, and releases the
    reservation on success and rollback paths. Firmware-clock lease acquisition
    also rejects faulted DVP leases and stale port ownership. The focused
    regression passed with host ASan/UBSan; formal G12 exited 0 and correctly
    remained `IN_PROGRESS`; required G03 then G05 regressions passed serially.
    During diagnosis, group 3150 exposed a directly constructed mailbox fixture
    that omitted its active DVP port mask; the fixture was corrected to model
    the existing lease invariant. The gate-run reports record a dirty working
    tree caused by pre-existing untracked files, which were left untouched.
    This remains software-only evidence: DVP/HDMI output is inactive, G13 is
    `NOT_STARTED`, and Pi 5 hardware remains `HARDWARE_NOT_VERIFIED`. See the
    DVP lease serialization result.
  - At source checkpoint `7f4bc01`, added a read-only DWC3 runtime verifier
    that reruns the existing structural and platform prerequisite checks,
    replaces synthetic IRQ/event callbacks with checks against the active
    MSI-X vector, PCI message/control state, generation-matched MIP/GIC route,
    event dispatcher and current xHCI capabilities. Fixtures cover successful
    modeled end-to-end route verification and rejection of a stale MSI-X
    handler, altered table message data, missing event handler and stale
    capabilities, while asserting no DWC3-register or MSI-X-table writes.
    PCI config reads can update the host bridge's config-window selector; no
    device initialization, xHCI activation or physical behavior is claimed.
    Focused Pi 5 host/ASan/UBSan tests, formal G12 (exit 0; `IN_PROGRESS`),
    and serial G03 then G05 regressions pass at this source checkpoint. An
    initial G03 attempt missed the AArch64 UEFI entry marker; the direct
    AArch64 rerun and subsequent complete G03/G05 rerun passed. Full G12
    remains `IN_PROGRESS`.
    G12 remains `IN_PROGRESS`, G13 `NOT_STARTED`, and hardware
    `HARDWARE_NOT_VERIFIED`.
  - At source checkpoint `2ef9cb2`, DWC3 startup now has a mandatory
    integrated USB-clock prerequisite separate from optional external
    clock/reset/PHY/power dependencies.
    Omitting external clocks from the pinned FDT profile no longer permits a
    caller to skip integrated USBH clock readiness. The unregistered USBH
    clock descriptors still have no verified rates, so startup remains
    fail-closed; fixtures cover the missing integrated-clock callback without
    MMIO writes or controller activation. Focused host/ASan/UBSan, formal G12,
    and serial G03 then G05 regressions pass at this source checkpoint. The
    first focused run exposed an outdated fixture expectation for the new
    mandatory bit; correcting the assertion resolved it without weakening
    coverage. No production USBH clock provider or physical USB behavior is
    claimed. See
    `logs/G12_RP1_DWC3_USB_CLOCK_GATE_RESULT.md`.
  - At source checkpoint `cb76a3a`, added an owner-checked Audio Out runtime
    binder connecting the existing clock lease, cache-backed DMA allocations,
    validated RP1 DMA mapping, staged DMA channel and MSI-X/MIP route. Runtime
    checks now require the exact atomically registered stage pointer, matching
    DMA resource/provider ownership, and a generation-matched active interrupt
    route; cache validation confirms page-pool ownership and physical-buffer
    identity without assuming identity DMA translation. Fixtures cover valid
    binding, non-identity DMA translation, stale stage, mapping and route
    generation rejection, and context clearing on failure. Focused host
    ASan/UBSan, formal G12 milestone verification and serial G03/G05 regressions
    pass at the committed source. The controller remains inactive; no
    production Audio Out caller, live DMA/IRQ, physical audio or Pi 5 behavior
    is established. G12 remains `IN_PROGRESS`, G13 `NOT_STARTED`, and hardware
    `HARDWARE_NOT_VERIFIED`. See
    `logs/G12_RP1_AUDIO_OUT_RUNTIME_RESULT.md`.
  - At source checkpoint `9f5ff82`, extended xHCI SuperSpeed and SuperSpeed
    Plus endpoint staging to support interrupt endpoints as well as the
    previously fixture-tested non-streaming bulk endpoints. Interrupt contexts
    validate the companion descriptor, burst and interval bounds, and
    `wBytesPerInterval`; the xHCI endpoint context receives the Max ESIT
    payload and matching average TRB length. Fixtures cover a HID boot
    interrupt-IN descriptor, malformed companion metadata, protocol-readiness
    gating, context fields, Configure Endpoint completion, and teardown of the
    additional DMA ring. The initial rerun exposed a stale DMA release-count
    expectation in the fixture; updating it for the additional owned ring
    resolved the failure without weakening assertions. Focused host ASan/UBSan,
    formal G12 milestone verification, and serial G03/G05 regressions pass.
    The controller remains inactive; live HID I/O and physical Pi 5 behavior
    are not established. SuperSpeed isochronous endpoints and streams remain
    unsupported; G12 remains `IN_PROGRESS`, G13 `NOT_STARTED`, and
    hardware `HARDWARE_NOT_VERIFIED`. See
    `logs/G12_RP1_XHCI_SUPERSPEED_INTERRUPT_RESULT.md`.
  - At source checkpoint `8281c37`, added bounded USB 3 endpoint companion
    descriptor parsing and xHCI SuperSpeed/SuperSpeed Plus bulk endpoint
    context staging. The parser validates descriptor adjacency, size, burst
    bounds, reserved fields and transfer-type metadata; endpoint staging accepts
    only 1024-byte bulk endpoints with a valid companion and no streams.
    Fixtures cover valid metadata, malformed and missing companions, endpoint
    context values, Configure Endpoint submission/completion and the bounded
    DMA allocator capacity needed by the additional endpoint resources.
    Focused host ASan/UBSan, formal G12 milestone verification and required
    serial G03/G05 regressions pass at the committed source. This does not
    activate a controller or establish live USB I/O or physical behavior.
    Isochronous SuperSpeed endpoints and streams remain unsupported;
    G12 stays `IN_PROGRESS`, G13 stays `NOT_STARTED`, and Pi 5 hardware remains
    `HARDWARE_NOT_VERIFIED`. See
    `logs/G12_RP1_XHCI_SUPERSPEED_BULK_RESULT.md`.
  - At source checkpoint `4470021`, added bounded FDT parsing and resolution
    for `#iommu-cells`/`iommus`, then extended HDMI resource ownership to map
    the pinned HVS MMIO window with rollback and teardown. Attachment validates
    the root-level HVS node and window, firmware clocks 4/16, zero-cell IOMMU4
    at `0x1000005200`, and HVS interrupts 2/9/16 through `disp_intr` at
    `0x107c502000` to GIC SPI 97. The AON L2 controller lookup now
    distinguishes the additional display L2 controller by its pinned
    physical window. The [pinned BCM2712 DTSI](https://github.com/raspberrypi/linux/blob/31a34885388835dce2460744d7961f2b9e7dc416/arch/arm64/boot/dts/broadcom/bcm2712.dtsi),
    [display-system DTSI](https://github.com/raspberrypi/linux/blob/31a34885388835dce2460744d7961f2b9e7dc416/arch/arm64/boot/dts/broadcom/bcm2712-ds.dtsi),
    and [Pi 5 board DTS](https://github.com/raspberrypi/linux/blob/31a34885388835dce2460744d7961f2b9e7dc416/arch/arm64/boot/dts/broadcom/bcm2712-rpi-5-b.dts)
    establish these bindings and keep HVS, HDMI, and `disp_intr` disabled in
    the base profile. Focused host ASan/UBSan and formal G12 checks pass, with
    G12 correctly remaining `IN_PROGRESS`; serial G03 then G05 regressions
    report `SOFTWARE_PASS`. This increment does not bind or enable live HVS
    interrupts, program IOMMU hardware, access display registers, or activate
    output. See
    `logs/G12_BCM2712_HVS_RESOURCES_RESULT.md`.
    Physical Pi 5 behavior remains `HARDWARE_NOT_VERIFIED`.
  - At source checkpoint `1a56b1f`, added read-only BCM2712 HVS clock
    prerequisite discovery. The verifier requires one enabled pinned HVS node,
    its translated MMIO identity, and the same validated firmware-clock
    provider used by the enabled HDMI ports; it checks the pinned core/display
    IDs 4 and 16 and requires both clocks enabled with nonzero rates before
    the existing HDMI prerequisite gate can unmask child interrupts. Fixtures
    cover disabled HVS, malformed bindings, query failure, response identity,
    state/rate failures and output clearing. The pinned base device tree
    disables HVS and HDMI, and the HVS/HDMI nodes declare no pinctrl state.
    This increment neither maps HVS nor binds IRQ/IOMMU resources, changes
    clocks or activates display output. See
    `logs/G12_BCM2712_HVS_CLOCKS_RESULT.md` for the focused evidence. Focused
    host ASan/UBSan, formal G12 including AArch64 QEMU, and serial G03 then G05
    regressions pass. G12 remains `IN_PROGRESS`; HVS resource mapping,
    IRQ/IOMMU, display modesetting and physical behavior remain unverified.
  - At source checkpoint `fbb021f`, added an owner-checked BCM2712 DVP
    quiesce operation. It requires every enabled HDMI port to be quiescent
    and refuses to act while a firmware-clock lease is held. It asserts the
    DVP reset before disabling its clock gate, preserves unrelated register
    bits, verifies readback, and rolls back on transition failure. If rollback
    cannot be verified, the lease remains faulted and owned. Fixtures cover
    precondition rejection, idempotence, unrelated-bit preservation,
    transactional rollback and retained ownership. Focused host ASan/UBSan,
    formal G12 including AArch64 QEMU, and serial G03 then G05 regressions pass.
    G12 remains `IN_PROGRESS`; no display activation or physical hardware
    behavior is claimed. See the DVP quiesce result.
  - At source checkpoint `8f960b4`, added an asynchronous, one-block SDHCI
    PIO lifecycle bound to the existing GIC-v2 route. Read/write starts retain
    caller buffers until explicit finish/cancel, service at most 128 FIFO
    words per ready interrupt, require both block data and DATA_END completion,
    restore the saved transfer/interrupt registers, and fault/reset after
    errors or cancellation. Cleanup rejects active transfers and IRQ bindings.
    Fixtures cover read/write data order, completion, duplicate-start and
    active-unbind rejection, cancellation, error faulting, read-buffer zeroing
    and     register restoration. Focused host ASan/UBSan, formal G12 including
    AArch64 QEMU, and serial G03 then G05 regressions pass; G12 remains
    `IN_PROGRESS`. These are software fixtures, not physical SDHCI, GIC, card,
    or electrical verification. DMA and high-speed modes remain unsupported.
  - At source checkpoint `0dc57b3`, added an explicit DWC3 attachment profile
    for the pinned host node after rejecting declared external clocks,
    resets, PHYs, power domains, VBUS supplies and `usb-psy-name` dependencies
    before MMIO mapping. In that validated absence profile, reset/clock/PHY/
    power callbacks are optional; any supplied callback must still pass, and
    interrupt-route and event-lifecycle callbacks remain mandatory. Synthetic
    unprofiled resources retain the strict six-callback requirement. Tests
    cover both profiles, failed optional callbacks, absent mandatory callbacks,
    `usb-psy-name` parsing/rejection and profile cleanup. Focused host
    ASan/UBSan, formal G12 including AArch64 QEMU, and serial G03 then G05
    regressions pass. G12 remains `IN_PROGRESS`; DWC3 startup and physical USB
    behavior are not claimed.
  - At source checkpoint `a196299`, extended read-only rate derivation for
    registered `RP1_CLK_GP0`–`RP1_CLK_GP5` beyond their parent-index-zero
    sources. The pinned alternate-parent indices exercised by fixtures are
    GP0 `{6, 10, 11, 15}`, GP1 `{6, 10, 12}`, GP2 `{6, 8, 11, 12, 14, 15}`,
    GP3 `{9}`, GP4 `{7, 11, 12, 15}`, and GP5 `{7, 8}`; these resolve only
    through already verified read-only PLL, SYS, ADC, PWM, UART, ETH-TSU,
    audio, I2S and MIPI clock-rate providers. Twenty valid alternate-parent
    cases, the existing parent-index-zero cases, a pinned but unsupported GP5
    VEC selection, unsupported parent holes, bounds, no-write behavior and
    mapping cleanup are fixture-tested. Focused host ASan/UBSan, formal G12
    including AArch64 QEMU, and serial G03 then G05 regressions pass. G12
    remains `IN_PROGRESS`; no GP output or physical clock behavior is claimed.
    See the GP clock result.
  - At source checkpoint `7d79c7c`, added read-only rate discovery for
    registered `RP1_CLK_GP0`–`RP1_CLK_GP5` (IDs 33–38). The pinned parent-index
    zero cases are XOSC for GP0/GP3/GP4/GP5, SDIO_TIMER for GP1, and
    SDIO_ALT_SRC for GP2; all other source selections remain fail-closed. The
    16-bit integer/fractional divider, zero-integer fallback, 100-MHz maximum,
    selector and divider bounds, unsupported parents, no-write behavior and
    mapping cleanup are fixture-tested. Focused host ASan/UBSan, formal G12
    including AArch64 QEMU, and serial G03 then G05 regressions pass. G12
    remains `IN_PROGRESS`; no GP output or physical clock behavior is claimed.
    See the GP clock result.
  - At source checkpoint `834f515`, added read-only rate discovery for the
    registered `RP1_CLK_ADC` descriptor (ID 30). The pinned descriptor's single
    XOSC auxiliary parent, 8-bit integer divider, zero-divider fallback and
    50-MHz maximum are fixture-tested, including malformed selectors and
    parents, divider and frequency bounds, map cleanup, and unchanged
    registers. Focused host ASan/UBSan, formal G12 including AArch64 QEMU, and
    serial G03 then G05 regressions pass. G12 remains `IN_PROGRESS`; no
    physical ADC clock or hardware behavior is claimed. See the
    ADC clock result.
  - At source checkpoint `037231b`, added read-only rate discovery for the
    registered `RP1_CLK_SLOW_SYS` (ID 13), `RP1_CLK_SDIO_TIMER` (ID 31), and
    `RP1_CLK_SDIO_ALT_SRC` (ID 32) descriptors. The pinned provider's XOSC and
    PLL_SYS parents, source/selector behavior, 8-bit integer divider,
    zero-divider fallback and 50-/200-MHz limits are fixture-tested. Tests
    cover disabled clocks, malformed selectors and source encodings, divider
    bounds, over-limit rates, PLL failure, map cleanup and unchanged clock
    registers. The USBH0/USBH1 microframe and suspend IDs are defined in the
    binding and register map but have no registered provider descriptors, so
    their rates remain unsupported and fail before MMIO. Focused host
    ASan/UBSan, formal G12 including AArch64 QEMU, and serial G03 then G05
    regressions pass. The initial host failure at group 1996 was a stale
    unsupported-ID fixture using `RP1_CLK_SYS + 1`, now the valid SLOW_SYS ID;
    it was corrected to probe an unassigned ID. G12 remains `IN_PROGRESS`; no
    physical clock behavior is claimed. See the
    SLOW_SYS and SDIO clock result.
  - At source checkpoint `60305f6`, added read-only rate discovery for the
    registered `RP1_CLK_PWM0` and `RP1_CLK_PWM1` descriptors (IDs 17 and 18).
    The pinned descriptors have no standard parents, use auxiliary parent 1
    for `PLL_VIDEO_SEC` and parent 2 for XOSC, and use a 16-bit fractional
    divider with a 16-bit integer limit, zero-integer fallback of `2^16`, and
    a 76.8-MHz maximum. Fixtures cover both clocks, both supported parents,
    fractional and zero-divider behavior, invalid/disabled parents, PLL and
    secondary-divider failures, selector/divider/frequency bounds, cleanup,
    and read-only registers. The pinned PWM0/1 nodes remain disabled; no PWM
    peripheral is activated. Focused host ASan/UBSan, formal G12 including
    AArch64 QEMU, and serial G03 then G05 regressions pass. G12 remains
    `IN_PROGRESS`; no physical PWM or clock behavior is claimed. See the
    PWM clock result.
  - At source checkpoint `9685dd7`, added read-only rates for the pinned
    `RP1_PLL_SYS_PRI_PH` (binding ID 6) and `RP1_CLK_UART` (ID 15)
    descriptors. The UART path validates enabled state, one-hot selector,
    supported SYS-phase/VIDEO/XOSC parent, PLL lock and power, phase output
    enable, 8-bit divider bounds and the 100-MHz maximum; zero divider follows
    the pinned 16-bit fractional-divider fallback. ID 8 is
    `RP1_PLL_AUDIO_PRI_PH`, not SYS_PRI_PH, and remains rejected before MMIO.
    Fixtures cover each valid parent, malformed selectors/parents/dividers,
    disabled/unlocked PLL and phase states, provider bounds, zero-divider
    behavior and read-only registers. A prior UART skip-init evidence entry
    transcribed the phase ID as 8; this increment records the correction
    against the pinned binding revision. Focused host ASan/UBSan, formal G12
    including AArch64 QEMU, and serial G03 then G05 regressions pass. G12
    remains `IN_PROGRESS`; no physical UART or clock behavior is claimed.
    See the UART clock result.
  - At source checkpoint `ecdf8bb`, added a bounded interrupt-driven
    DesignWare SPI transfer lifecycle while preserving the polling API.
    Transfers require the owner-bound IRQ route to be enabled, service
    TX-empty/RX-full/error events with FIFO and outstanding-transfer bounds,
    and retain caller-buffer ownership until completion or safe cancellation.
    Fixtures cover transfer completion, cancellation, controller errors,
    cleanup exclusion, and failed error-status acknowledgement. The
    group-2004 regression was traced to idle interrupt status being recorded
    before a failed clear; status is now recorded only after successful
    acknowledgement, and the callback assertion is isolated from earlier
    fixture events. Focused host ASan/UBSan, formal G12/AArch64 QEMU, and
    serial G03 then G05 regressions pass. G12 remains `IN_PROGRESS`; no
    physical SPI behavior is claimed. See the
    SPI transfer result.
  - At source checkpoint `449d571`, DDC resource discovery now rejects an
    otherwise valid controller when its required direct `simple-bus` parent
    is disabled. The register-model fixture verifies rejection before any
    MMIO map call, then verifies attach/cleanup after restoring the parent
    status. Focused host tests and ASan/UBSan, formal G12 including AArch64
    QEMU, and serial G03 then G05 regressions passed against source checkpoint
    `d5b9e22`. G12 remains `IN_PROGRESS`; this does not enable the pinned,
    disabled DDC/HDMI nodes or establish production display readiness.
  - At source checkpoint `6b3016f`, extended the firmware mailbox fixture to
    simulate SET_CLOCK_RATE being applied before its response is lost. The
    lease-acquire path fails closed, retains the DVP and firmware-clock owners,
    and prevents both HDMI-resource and mailbox teardown; retry is rejected
    while the mailbox remains faulted, so no uncertain state is released.
    Focused host ASan/UBSan and formal G12/AArch64 QEMU checks pass, followed
    serially by G03 and G05 regressions. No physical firmware transaction or
    display activation is claimed.
  - At source checkpoint `cbf4d34`, corrected the BCM2712 firmware mailbox
    clock GET request/response lengths and added the production SET_CLOCK_RATE
    path behind the nested DVP and firmware-clock leases. The public generic
    mailbox callback remains GET-only; SET requires the matching live DVP and
    firmware-clock owners, all enabled HDMI ports quiescent, and unchanged
    firmware-reported enable states. Fixtures validate 4-byte GET requests with
    8-byte responses, 12-byte SET requests with 8-byte responses, rejection of
    unleased SET, and leased rate change plus restoration. Focused host
    ASan/UBSan, formal G12/AArch64 QEMU checks, and serial G03 then G05
    regressions pass. G12 remains `IN_PROGRESS`; no physical firmware
    transaction or display activation is claimed. See the formal G12
    result and SHA-256 manifest.
  - At source checkpoint `6eab177`, added an owner-bound BCM2712 HDMI
    firmware-clock rate lease nested under the shared DVP lease. Requests must
    cover every enabled HDMI port, preserve both firmware-reported enabled
    states, and use the bounded SET_CLOCK_RATE callback only after validating
    the original rates. Release restores and verifies original rates before
    dropping ownership; ambiguous failures retain the lock for retry, while
    DVP release and resource cleanup are excluded. Fixtures cover state-change
    rejection, rate apply/restore, cleanup exclusion, apply-then-error rollback,
    and failed rollback with retained ownership and successful retry. The
    production mailbox remains GET-only and no clock setter, DVP activation,
    display-register access, or display activation is connected. Focused
    host ASan/UBSan, formal G12/AArch64 QEMU checks, and serial G03 then G05
    regressions pass. G12 remains `IN_PROGRESS`; physical HDMI behavior remains
    unverified. See the formal G12 result and
    SHA-256 manifest.
  - At source checkpoint `73f5527`, hardened the RP1 GEM network adapter to
    revalidate its active controller, DMA publication, ring, and interrupt
    ownership before every frame send or receive. The controller, publication,
    and IRQ must reference the same GEM resources and rings; stale or mismatched
    bindings fail before descriptor or MMIO operations. Fixtures cover missing
    controller/IRQ linkage without submitting frames, plus the existing
    GEM-to-network-stack DHCP path. Focused host ASan/UBSan, formal
    G12/AArch64 QEMU checks, and serial G03 then G05 regressions pass. G12
    remains `IN_PROGRESS`: there is still no production GEM startup caller or
    verified PHY power-feedback provider, so Ethernet remains inactive and no
    physical network operation is claimed. See the
    formal G12 result and
    SHA-256 manifest.
  - At the DVP prerequisite increment based on checkpoint `bba1d37`, added an
    owner-bound, exclusive lease covering one or both enabled ports of the
    shared DVP provider. Acquire and release require quiescence for every
    leased port, validate their
    read-only reset/gate state, and retain ownership on state drift until the
    state is restored and release is retried. Mapping cleanup reserves the
    provider against concurrent leases. Invalid ports, owner conflicts,
    non-quiescent acquisition and read failures fail closed with rollback;
    cleanup is rejected while a lease is held. The lease never writes DVP or
    firmware registers, and fixtures verify unchanged register state across
    acquisition, failure and teardown. Focused host tests, including
    ASan/UBSan, pass. No display activation or physical hardware behavior is
    claimed; G12 remains `IN_PROGRESS`.
  - At the AON/DVP lifecycle increment based on source checkpoint `8d44abb`,
    made attachment and resource teardown mutually exclusive with atomic
    detached/attaching/attached/cleaning states. AON attachment reserves the
    shared HDMI resource before validating or mapping it; HDMI teardown
    reserves it before validation/unmapping; AON teardown releases the
    reservation only after unmapping and resetting its controller state.
    This closes the check-then-unmap race that could otherwise invalidate the
    provider while AON attachment or cleanup was in progress. Fixtures cover
    cleanup refusal during attach/clean reservations, attachment refusal while
    cleanup is reserved, cleanup refusal while AON is attached, and release
    after AON teardown. Focused host tests, including ASan/UBSan, pass. The
    lifecycle does not write DVP or firmware registers, activate HDMI, or
    establish physical hardware behavior; G12 remains `IN_PROGRESS`.
  - At source checkpoint `2fba7c3`, corrected the HDMI DVP status verifier to
    map the pinned `brcm,brcm2711-dvp` provider resource at
    `0x107c700000/0x10`. The previous verifier read HDMI's unrelated `hd`
    resource at `0x107c720000`. The HDMI resource owner now maps the shared DVP
    provider once, revalidates its exact FDT identity, bounds and 108-MHz parent
    clock, and rolls back both port mappings if provider mapping fails. Teardown
    releases the provider mapping with the HDMI resources. Fixtures reject the
    wrong provider address and cover shared ownership, read failures, mapping
    rollback and teardown; the DVP state verifier performs reads only.
    Focused host ASan/UBSan tests pass. No DVP or firmware clock writes,
    display activation or physical hardware behavior is claimed. G12 remains
    `IN_PROGRESS`.
  - At source checkpoint `51e9ccd`, added an owner-bound BCM2711 AON L2
    lifecycle for the pinned HDMI child interrupts 1-3, 7-8 and 11-15.
    Attachment validates the exact controller resource, level-high GIC SPI
    239/INTID 271 configuration, HDMI FDT identity and fully masked child
    lines. Child binding does not unmask hardware; enable first requires a
    callback and read-only DVP/firmware-clock readiness, then performs bounded
    pending-clear, mask, parent-registration and GIC-enable sequencing while
    preserving prior GIC priority/target/group/enable state. Level dispatch
    masks and acknowledges only owned child sources; unexpected unmasked
    sources disable the parent fail-closed. Rollback, reentrant calls, shared
    child ownership and teardown are fixture-tested. Focused host tests and
    ASan/UBSan, formal G12/AArch64 QEMU checks, and serial G03 then G05
    regressions pass at this source checkpoint. The AON path remains host
    register-model evidence only: both HDMI nodes are disabled in the pinned
    board DTS, there is no production display caller, no DVP/firmware clock is
    activated, and physical display behavior remains unverified. G12 stays
    `IN_PROGRESS`. See the formal G12 result and
    SHA-256 manifest.
  - At source checkpoint `9d3cdb3`, added a read-only BCM2712 SDIO1 pinctrl
    verifier for the pinned C0 and D0 layouts. It validates unique enabled
    pinctrl nodes, their FDT parent/compatible and exact MMIO bounds, then
    checks GPIO3/4/5 `sd_card_g` mux selection, AON GPIO5 pull-up, and eMMC
    CMD/DAT0-3 pull-ups. A public recheck detects stale register state;
    mismatches fail closed, partial mapping rolls back, cleanup is tied to the
    original mapping owners, and fixtures confirm that no register is written.
    Host ASan/UBSan, formal G12/AArch64 QEMU checks, and serial G03 then G05
    regressions pass. G12 remains `IN_PROGRESS`: this is only a software
    state verifier, not an exclusive pinctrl lease or production SDHCI caller.
    The regulator helper still lacks safe GPIO mux ownership; SD storage stays
    inactive, and no physical pin or card behavior is claimed.
    See the formal G12 result and
    SHA-256 manifest.
  - At source checkpoint `ee5a1f8`, strengthened BCM2712 SD storage
    completion handling to re-sample the owner-bound active-low card-detect
    GPIO after SDHCI reports transfer completion and the data line becomes
    idle. If removal or an unreadable detect state is observed, the operation
    fails, attempts bounded data-line recovery, marks storage faulted, and
    restores the saved SDHCI registers. Mock fixtures remove the card after
    the pre-transfer check and inject a card-detect read failure after data
    completion; both verify failure, reset, register restoration, and
    teardown. Focused ASan/UBSan and formal G12/AArch64 QEMU checks pass;
    serial G03 then G05 regressions pass. G12 remains `IN_PROGRESS`; this is
    fixture/QEMU software evidence only, not physical card-removal behavior.
    See the SD storage removal result.
  - At source checkpoint `0493ee7`, added bounded SDIO2 function-1 CMD53
    block-mode PIO transfers. The helper validates the function-1 FBR block
    size and caps block-ready polling at 16,384 polls per request; the
    512-block/32-poll boundary passes and over-budget requests are rejected
    before prerequisite callbacks or command submission. Fixtures cover
    two-block reads/writes, 512-block CMD53 zero-count encoding, FBR mismatch,
    polling bounds, and SDHCI register restoration. Focused ASan/UBSan,
    formal G12/AArch64 QEMU, and serial G03/G05 regressions pass. G12 remains
    `IN_PROGRESS`; no physical SDIO/WLAN behavior is claimed.
    See the CMD53 block-mode result.
  - At source checkpoint `3fe68d8`, strengthened the RP1 DesignWare I2C
    transfer lifecycle to require bounded confirmation from `ENABLE_STATUS`
    before beginning a transfer or releasing caller-buffer ownership during
    cancellation or completion. A stuck enabled status prevents transfer
    start without changing the target; cancellation retains active state and
    buffer pointers until disabled status is observed. Focused host ASan/UBSan,
    formal G12/AArch64 QEMU checks, and serial G03/G05 regressions pass. G12
    remains `IN_PROGRESS`; this is software-only register-model evidence and
    does not verify physical I2C behavior.
    See the I2C disable-status result
    and formal G12 result.
  - At source checkpoint `f2197b9`, added a bounded interrupt-driven
    DesignWare I2C master-transfer lifecycle while preserving the polling API.
    Transfers require the owner-bound IRQ route to be enabled, queue TX and
    read commands from TX_EMPTY, drain only outstanding RX data, preserve
    STOP_DET until reads are accounted for, and fail closed on controller
    errors. Cancellation and cleanup retain caller-buffer ownership if the
    controller cannot be disabled. Host fixtures cover combined TX/RX,
    repeated-start/STOP commands, early STOP, abort, overlapping-operation
    rejection, cancel retry and resource lifetime; host ASan/UBSan and formal
    G12/AArch64 QEMU checks passed with G12 still `IN_PROGRESS`, followed by
    serial G03/G05 `SOFTWARE_PASS`. This is register-model/QEMU software
    evidence only; no physical I2C bus or Raspberry Pi behavior is claimed.
    The polling API remains available, and SPI remains polling-based.
    See the formal G12 result and
    SHA-256 manifest.
  - At source checkpoint `31c7fcd`, added the pinned RP1 clock-binding IDs
    for USBH0/USBH1 microframe and suspend clocks and fixture checks that all
    four unsupported IDs fail closed before any clock-provider MMIO mapping.
    No USBH frequency is inferred: the pinned clock provider has register
    definitions but no registered descriptors for these clocks. The focused
    host suite passed with ASan/UBSan; formal G12/AArch64 QEMU checks passed
    with G12 still `IN_PROGRESS`, followed by serial G03 and G05
    `SOFTWARE_PASS`. This does not establish DWC3 clock, PHY, power, reset,
    event or host readiness. The next software increment is the supported
    interrupt-driven DesignWare I2C transfer path; keep USB host mode disabled.
    See the formal G12 result and
    SHA-256 manifest.
  - At source checkpoint `bbe0e19`, extended the bounded Clause 22 PHY read
    with BMCR register 0 and a fail-closed power prerequisite callback using
    the dedicated GEM `power_context`. The verifier rejects a PHY reporting
    BMCR power-down and accepts a clear power-down bit; fixtures cover both
    states and startup callback routing. Focused host tests passed with
    ASan/UBSan, formal G12/AArch64 QEMU checks passed with G12 still
    `IN_PROGRESS`, and required G03 then G05 regressions passed serially at
    this source checkpoint. This is modeled PHY register evidence only: it
    does not establish an external supply rail, GEM MAC power, a live link,
    Ethernet traffic, or physical hardware behavior. See the
    formal G12 result and
    SHA-256 manifest.
  - At source checkpoint `0123531`, added a callback-compatible GEM PHY-status
    verifier for the existing startup prerequisite interface. It checks the
    RP1/resource/clock-context identity before invoking the bounded status
    read, clears status on invalid context, and accepts a valid PHY with link
    down. Fixtures verify this behavior and rejected-context output clearing.
    Host ASan/UBSan and formal partial G12 passed; serial G03/G05 regressions
    passed on this source checkpoint. The adapter does not create a production
    startup caller, so GEM remains fail-closed and inactive.
    See the PHY verifier result
    and SHA-256 manifest.
  - At source checkpoint `33b22b1`, verified bounded Clause 22 PHY identity
    and link-status readout against the pinned Linux PHY-ID validity rule.
    All-zero IDs are accepted; IDs whose low 29 bits are all ones are rejected.
    BMSR is read twice to account for its latched-low status bits, and link-down
    is valid status rather than a read failure. Host fixtures passed with
    ASan/UBSan, formal G12 remained `IN_PROGRESS`, and serial G03/G05
    regressions passed on this source checkpoint. This remains register-model
    evidence only: the GEM startup path does not call the helper, no link or
    Ethernet traffic is established, and physical hardware remains
    `HARDWARE_NOT_VERIFIED`. See the
    PHY status result and
    SHA-256 manifest.
  - At source checkpoint `6c587f0`, added bounded Clause 22 GEM MDIO
    reads/writes. The transaction revalidates the GEM/PHY FDT relationship,
    RP1 BAR ownership, mapping bounds and live clock rates; derives the MDC
    divider from the verified SYS rate; requires TX/RX stopped and MDIO idle;
    serializes access; and restores the original divider and management-enable
    state on success or failure. Cleanup is excluded while an operation is
    active. Fixtures cover command encoding, read/write data, invalid PHY and
    register addresses, missing ownership/bounds, disabled runtime clock,
    busy timeout, MMIO failures, serialization and cleanup exclusion.
    `bash scripts/test-pi5-host.sh`, formal G12/AArch64 QEMU checks, and
    serial G03 then G05 regressions pass at this source checkpoint. This is
    register-model evidence only: GEM is not started and no PHY link or
    physical Ethernet behavior is claimed. See the
    GEM MDIO result and
    SHA-256 manifest.
  - At source checkpoint `ae95feb`, added a read-only HDMI clock-prerequisite
    verifier that combines per-port DVP reset/gate readiness with the
    firmware-reported HDMI and BVB clock states and rates. It clears outputs
    on failure and refuses to query firmware for an invalid port or a DVP
    port that is not ready. Fixtures cover success, invalid port, disabled
    HDMI/BVB clocks, zero rates and DVP-not-ready behavior. The helper does
    not write clocks, program a mode, bind interrupts or activate display.
    `bash scripts/test-pi5-host.sh` passed with ASan/UBSan; formal G12 and
    AArch64 QEMU checks passed with G12 still `IN_PROGRESS`; required G03 then
    G05 regressions passed serially. See
    clock-prerequisite result
    and SHA-256 manifest.
    Firmware/MMIO behavior remains fixture-only and
    physical Raspberry Pi 5 hardware remains `HARDWARE_NOT_VERIFIED`.
  - At source checkpoint `6f4c290`, gated RP1 DMA interrupt-route binding on
    a bounded, read-only controller reset/idle check and nonzero runtime DMA
    and SYS clock rates. The binding resolves and revalidates the DMA node
    before querying its clocks, and performs no MSI-X/MIP route changes until
    all checks pass. Mock-MMIO fixtures verify that a stuck reset and disabled
    DMA clock reject binding before route side effects. Focused host tests
    pass with ASan/UBSan; formal G12 and AArch64 QEMU checks pass with G12
    remaining `IN_PROGRESS`; G03 then G05 regressions pass serially on the
    code-identical implementation checkpoint. This does not start the
    controller or submit a descriptor. The pinned base tree has no enabled
    production DMA consumer, so channel submission/completion remains gated.
    See the
    DMA IRQ readiness result
    and SHA-256 manifest.
  - At source checkpoint `ca9a562`, implemented an FDT-bound BCM2712
    firmware-property mailbox transport for the previously tested HDMI clock
    query contract. Attachment validates the BCM2712 mailbox node/resource,
    its supported simple-bus DMA ancestry, and a 16-byte-aligned firmware DMA
    buffer whose complete translated address fits the mailbox's 32-bit
    register format. Transactions are serialized, cache-synchronized, bounded
    at transmit/receive, and validate the returned mailbox word, message
    header, property tag, response-size value 13 (12-byte payload plus the
    firmware response marker), clock ID and reserved word. A submitted request
    with uncertain completion or malformed response
    retains the DMA buffer and mapping; cleanup cannot race an active request.
    Fixtures cover success, non-identity DMA translation, invalid/missing DMA
    ranges, attach rollback, cache-sync failures, firmware rejection,
    malformed response size/identity, mailbox-word mismatch, active-operation
    teardown rejection and timeout retention. `bash scripts/test-pi5-host.sh`
    passed with ASan/UBSan; formal G12/AArch64 QEMU checks passed, followed by
    serial G03 and G05 regressions. See the
    mailbox result and
    SHA-256 manifest.
    This remains software-fixture evidence: no physical mailbox transaction,
    firmware clock write, display activation or Raspberry Pi 5 hardware
    behavior is established. G12 remains `IN_PROGRESS`, G13 remains
    `NOT_STARTED`, and hardware remains `HARDWARE_NOT_VERIFIED`.
  - At source checkpoint `2493e81`, added a bounded HDMI firmware-clock
    query contract for the two pinned firmware clock IDs, 13 (HDMI) and 14
    (BVB). It requests GET_CLOCK_STATE and GET_CLOCK_RATE with the pinned
    three-word clock payload, validates the response clock ID and reserved
    payload word, rejects an enabled clock reporting zero rate, and clears all
    outputs on any failure. Fixtures cover the four-request sequence, rates,
    callback failure, mismatched response identity and invalid port. Focused
    ASan/UBSan tests, formal partial G12/AArch64 QEMU checks, and serial
    G03/G05 regressions passed. The property provider is injected: no mailbox
    transport or real firmware request is implemented, no clock is written
    or enabled, and display remains inactive. Next implement bounded,
    FDT-bound firmware mailbox transport only after validating its DMA
    address translation, cache ownership, response integrity and timeout
    behavior. See `logs/G12_BCM2712_HDMI_CLOCK_QUERY_RESULT.json`.
  - At source checkpoint `fc6a2dc`, added a read-only BCM2712 DVP readiness
    verifier for each enabled HDMI port. It validates the owner-mapped shared
    DVP window, checks the pinned reset-control and clock-gate bits, and fails
    closed on invalid ownership, missing mappings, asserted reset, disabled
    gate, or MMIO read failure. Fixtures cover both ports, HDMI1-only mapping,
    disabled ports, each not-ready bit and read failure. The implementation
    follows the pinned DVP driver's reset-simple defaults and `CLK_GATE_SET_TO_DISABLE`
    polarity; it performs no register writes. Focused ASan/UBSan tests, formal
    partial G12/AArch64 QEMU checks, and serial G03/G05 regressions passed.
    This does not query or enable firmware-managed HDMI/BVB clocks, bind the
    HDMI IRQs, program a mode, or activate display output. No trusted Wi-Fi
    firmware image or local firmware source was found, so firmware upload and
    Wi-Fi remain unimplemented; no firmware binary was downloaded. Next
    inspect the pinned Raspberry Pi firmware property-channel interface and
    local platform support before implementing a bounded HDMI clock-state
    query; do not write clocks or enable display until the full dependencies
    are verified. See
    `logs/G12_BCM2712_DVP_STATUS_RESULT.json`.
  - At source checkpoint `25fb117`, added an owner-bound, bounded SDIO2
    function-1 CMD53 byte-mode PIO transfer API. It validates function state,
    argument/address/count bounds, buffer direction, and every required
    provider before issuing a command; checks R5 status; handles partial
    final words without accessing beyond the caller's buffer; restores SDHCI
    block-size/count/transfer-mode registers; and marks card state unknown on
    an ambiguous transfer failure until CMD0 recovery succeeds. Tests cover
    incrementing reads, fixed-address writes, exact 9-byte FIFO tails,
    argument encoding, range rejection, missing-DMA-provider rejection,
    transfer-error output clearing, register restoration, and supply-disable
    exclusion during active host operations. `bash scripts/test-pi5-host.sh`
    passed with ASan/UBSan; formal G12 partial verification and AArch64 QEMU
    checks passed; G03 then G05 regressions passed serially. See
    `logs/G12_RP1_SDIO2_CMD53_RESULT.json`. This is mock-SDHCI fixture
    evidence only: no production readiness providers, card transaction,
    DMA/interrupt activity, firmware upload, WLAN activation, or physical
    Wi-Fi is established. G12 remains `IN_PROGRESS`, G13 remains
    `NOT_STARTED`, and hardware remains `HARDWARE_NOT_VERIFIED`. No trusted
    Wi-Fi firmware input was available in the repository; do not fetch or
    fabricate a vendor firmware binary.
  - At source checkpoint `ea5412c`, completed the owner-bound SDIO2 CMD52
    and CCCR function-1 lifecycle. The host validates card/supply/mapping
    ownership and runs prerequisite callbacks only for valid requests.
    Function-1 enable uses CCCR IO Enable readback, bounded IO Ready polling,
    and rollback; disable likewise verifies readback/readiness. Failed
    rollback marks card state unknown, and a successful CMD0 reset clears the
    state. Mock-SDHCI fixtures cover negotiated CMD5, owner-bound CMD52,
    delayed function readiness, disable, timeout with successful rollback,
    command failure with rollback, rollback failure and reset recovery,
    missing providers, invalid function/card state, and lifecycle cleanup.
    `bash scripts/test-pi5-host.sh` passed with ASan/UBSan; formal G12
    partial verification passed with AArch64 QEMU; G03 then G05 regressions
    passed serially. Evidence is in
    `logs/G12_RP1_SDIO2_CCCR_RESULT.json`. Provider callbacks and SDHCI are
    modeled fixtures only; no production DMA/interrupt readiness provider,
    physical SDIO transaction, WLAN activation, firmware, or Wi-Fi is
    established. G12 remains `IN_PROGRESS`, G13 remains `NOT_STARTED`, and
    hardware remains `HARDWARE_NOT_VERIFIED`. Next inspect the remaining
    documented SDIO CMD53/data path and its dependencies; keep activation
    fail-closed until production prerequisites are verified.
  - At source checkpoint `b43cb92`, completed the owner-bound SDIO2 command
    context. Host attachment initializes embedded SDHCI command resources and
    copies the exact two-region mapping, interrupt and clock identity without
    accessing registers. Query/negotiate entry revalidates the live FDT
    resources and mapping, requires active controller reset, power and clock
    state plus bounded read-only status checks, and holds the host lifecycle
    guard. Cleanup rejects active power/clock state and incomplete lifecycle
    operations. Fixtures cover mapping/resource binding, inactive and
    mismatched-owner rejection, output clearing, active-state teardown
    exclusion, and lifecycle exclusion. Focused ASan/UBSan host tests, formal
    G12/AArch64 QEMU checks, and serial G03 then G05 regressions pass. G12
    remains `IN_PROGRESS`; no successful physical SDIO command, CCCR access,
    WLAN activation, or Pi 5 hardware behavior is claimed.
  - At source checkpoint `762fb0b`, added bounded SDIO CMD5 operating-
    condition negotiation after the R4 query. It intersects the card OCR with
    the host's supported voltage window, polls ready at a fixed 10-ms interval
    under a strict 250-attempt maximum, and rejects changes in the OCR,
    function count, or memory-presence flag. All outputs remain clear on
    timeout, malformed responses, unsupported voltage, or callback failure.
    Fixtures cover successful delayed readiness, changed function count/OCR,
    no voltage overlap, and bounded timeout. Focused host/ASan/UBSan tests,
    formal G12/AArch64 QEMU checks, and serial G03 then G05 regressions pass.
    An initial group-3000 test failure was an incorrect fixture expectation:
    the delay mock counts successful callbacks only, so a configured failure
    correctly leaves its success count unchanged. The assertion was corrected
    without changing implementation behavior, then all focused and gate tests
    passed.
    This remains a protocol primitive: it does not activate SDIO2 or the WLAN
    supply, write CCCR, submit CMD53, load firmware, or establish Wi-Fi.
    G12 remains `IN_PROGRESS`.
  - At source checkpoint `d6871a0`, DWC3 inspection confirmed the pinned
    `rp1-common.dtsi` nodes are disabled by default and expose no reset, clock,
    PHY, power-domain, or VBUS-supply provider for a production startup check.
    Attachment continues to reject declared unsupported dependencies, so USB
    host mode, IMAN, and event/doorbell delivery remain disabled. Advanced to
    the independent WLAN path with a bounded SDIO CMD5 query that decodes the
    R4 OCR, function count, memory-presence, and ready fields. It sends CMD5
    with argument zero, rejects cards without an I/O function or a supported
    voltage window, and clears output on failure. Fixtures cover valid
    function/voltage responses, optional memory/ready bits, malformed
    no-function/no-voltage responses, and command timeout. Focused host tests
    with ASan/UBSan, formal G12 milestone and AArch64 QEMU checks, and serial
    G03 then G05 regressions pass. This query does not enable WLAN supply,
    negotiate OCR, access CCCR, submit CMD53, load firmware, or establish
    physical Wi-Fi. G12 remains `IN_PROGRESS`.
  - At source checkpoint `fcbbcb1`, added owner-checked, read-only RP1 DMA
    verification for both the core (`DMA`) and configuration (`SYS`) clock
    rates, then combined it with the bounded controller reset/idle check.
    The combined verifier requires reset status clear, global DMA/interrupts
    disabled, all channel-enable bits clear, valid owned DMA resources, and
    nonzero clock rates; it zeroes rate outputs on any failure. Fixture tests
    cover valid rates, disabled DMA/SYS clocks, an out-of-BAR clock provider,
    stuck reset, global enable and active channel state. Diagnosed the prior
    host-test group 1975 failure as a shared FDT fixture inconsistency: its
    DMA setup had replaced the RP1 root-bus node with a fixed-clock node while
    later peripheral fixtures still used that index as an ancestor. The
    fixture now retains one RP1 root bus and places the fixed clock at a
    separate node. `bash scripts/test-pi5-host.sh`, formal G12 milestone
    verification including AArch64 QEMU, and serial G03 then G05 regressions
    pass. G12 remains `IN_PROGRESS`; no clock was enabled, reset issued,
    controller/channel activated, descriptor submitted, or physical behavior
    claimed. There is still no production DMA consumer.
  - Added read-only RP1 AXI-DMA controller idle verification at source
    checkpoint `6b1a766`. Against the pinned DesignWare driver, the RP1 DMA
    node provides separate `core-clk`/`cfgr-clk` clocks and is disabled in the
    base device tree; the controller self-reset status is polled before global
    and channel enablement. The verifier bounds reset-status polling, then
    requires the mapped controller's reset status to clear, global DMA and
    interrupt enable bits to remain clear, and all eight channel-enable bits
    to be idle. It performs no writes and refuses a missing/invalid mapping,
    bound IRQ, invalid poll bound, non-idle controller or MMIO read failure.
    Host/ASan/UBSan and AArch64 builds pass; formal G12 remains `IN_PROGRESS`,
    and serial G03/G05 regressions pass. No clock is enabled, reset is issued,
    or controller/channel is activated. A production owner-checked dual-clock
    readiness path and enabled DMA consumer remain outstanding.
  - RP1 DMA stages now retain the exact transfer buffer, offset, length, and
    direction. A stage cannot be marked submitted until its exact data range
    has synchronized for the device; device-to-memory completion synchronizes
    that range for the CPU before ownership is released. Cache-sync failures
    preserve staged or in-flight ownership for retry. Fixtures cover a
    nonzero-offset transfer range, pre-submit sync failure, and completion
    sync failure/retry. This closes the data-buffer cache-maintenance gap in
    the staged lifecycle. Host/ASan/UBSan, AArch64 build, G00, partial G12
    with AArch64 QEMU, and serial G03/G05 pass at source commit `7348dd2`.
    There is still no production channel submit path or caller, controller
    activation, or physical DMA evidence.
  - Added an owner-matched GEM DMA/cache readiness verifier for the existing
    AArch64 page-pool allocator. It accepts only the allocator's exact
    allocate/release/sync adapters, the matching ready cache provider and pool
    owner, idle GEM rings, and all four ring allocations whose cache-line
    ranges remain within pages owned by that provider. A dedicated DMA-sync
    callback context can be used by GEM prerequisite checks. It inspects
    metadata and cache-line bounds without issuing cache operations, starting
    DMA, or accessing GEM registers. Host/ASan/UBSan tests fail closed on
    non-AArch64; G12 AArch64 QEMU verifies the valid provider and rejects
    mismatched owners, missing synchronization callbacks, invalid poll bounds
    and unowned pages. This remains software-only evidence: no production GEM
    caller, live DMA, or Pi 5 behavior is established.
    See the DMA/cache readiness result.
  - RP1 DesignWare SPI/I2C attachments now require each translated child
    interval to be wholly owned by exactly one active RP1 memory BAR, before
    any target or clock-provider MMIO mapping. Direct attachment rejects
    missing BAR ownership; clocked attachment rejects an interval at the BAR
    end without mapping either region. Existing valid attach fixtures still
    pass. This remains software-only evidence; no physical bus operation is
    claimed. See the SPI/I2C BAR ownership result.
  - Added an owner-held GEM PHY reset operation using the attached RP1 GPIO
    provider and reset lease. It validates that the mapped GEM resource and
    GPIO provider still match the same enabled FDT tree, requires the pinned
    active-low polarity, bounds the assertion interval to 1–1000 ms, asserts
    reset, waits the exact FDT duration, deasserts it, and verifies the GPIO
    output. A separate read-only prerequisite callback verifies the completed
    operation and retained lease; the startup checker accepts its dedicated
    callback context. Fixtures cover the 5-ms interval, duplicate execution,
    line-state mismatch, zero poll bound, failed delay, unsupported polarity,
    excessive duration, and GPIO cleanup. Focused host/ASan/UBSan tests pass.
    This operation is not yet connected to a production GEM startup caller,
    does not establish PHY link. The bounded MDIO contract is tested
    separately, but GEM remains inactive and hardware behavior remains
    unverified. See the
    GEM reset sequencing result.
  - Added read-only runtime RP1 GEM clock-rate verification. The checker
    revalidates the four required clock IDs and their common owned RP1 clock
    provider, re-reads the live SYS, ETH and ETH-TSU provider registers, and
    requires each rate to match the rate captured at GEM resource attachment.
    A dedicated clock callback context lets the verifier be wired into GEM's
    prerequisite checks without changing the context used by the other
    providers. Fixtures reject an empty context, zero poll limit, a non-GEM
    node, mixed clock providers, altered or disabled ETH state, a changed
    clock ID, and an unavailable provider mapping.
    The checker only reads and temporarily maps clock-provider registers; it
    does not toggle a clock or access GEM registers. Host and ASan/UBSan tests
    pass. Formal G12 partial verification and G00 passed, followed by serial
    G03 then G05 regressions at source commit `3ea4c2e`; G12 remains
    `IN_PROGRESS`. All 125 entries in each current gate manifest match. See
    the runtime clock verification result.
    This remains software evidence only: no production GEM startup caller or
    Pi 5 clock behavior is established, and reset, PHY, power, DMA and
    interrupt readiness remain separately gated.
  - RP1 PCI ownership now maps each assigned, translatable memory BAR during
    parent attachment, after masking MSI-X and before marking RP1 ready.
    Unassigned, I/O, oversized, untranslatable, or unavailable BARs are not
    treated as child resources. Later child attachment performs no PCI BAR
    probing or acquisition; each translated FDT interval must fit wholly in
    exactly one already-owned mapping or attachment fails closed. Host and
    ASan/UBSan fixtures verify the synthetic BAR0/BAR1 setup, reject the DMA
    child when its owning BAR is absent, confirm full-interval containment when
    owned, and check that child attachment performs no PCI configuration writes.
    This is software register-model evidence only; synthetic BAR assignments
    do not establish Raspberry Pi 5 BAR assignments or hardware behavior.
    G12 remains `IN_PROGRESS`.
  - At verified checkpoint `aab1131`, RP1 root discovery matches the pinned
    DTS: the enabled node named `rp1` is a `simple-bus` with `ranges` directly
    beneath the validated BCM2712 PCIe host. WLAN, CFE, audio, DMA and I2S
    ancestry checks now share this validated lookup; fixtures no longer add
    the nonexistent `raspberrypi,rp1` root compatible. Focused host/ASan/UBSan,
    formal G12 milestone verification, and serial G03 then G05 regressions
    pass. A cold AAVMF startup exceeded the former 10-second QEMU test bound;
    the bounded timeout is now 30 seconds and the serial regressions pass.
    This proves FDT ownership ancestry only. BAR-to-child resource containment
    and live hardware behavior remain unverified. See the
    RP1 root-bus ownership result.
  - Added an owner-checked, read-only RP1 GPIO output-state verifier for a
    leased pin. One bounded read of each GPIO control, pad, RIO output and
    output-enable register verifies GPIO function selection, normal output
    overrides, output-enabled pad state, output direction and the expected
    latch value. The verifier uses the pin lease and GPIO lifecycle guard, so
    another owner or concurrent cleanup cannot access the registers. Host
    and sanitizer fixtures cover a missing lease, wrong owner, wrong function,
    override and pad mismatches, input direction, output-value mismatch, MMIO
    read failure and teardown exclusion. Focused host/ASan/UBSan tests pass.
    Formal G12 remains `IN_PROGRESS`; serial G03 and G05 regressions pass at
    source checkpoint `b1f653e`.
    This verifies software-visible GPIO configuration only; it is not evidence
    of external WLAN rail voltage or Raspberry Pi hardware behavior.
  - Added a bounded, read-only SDIO2 reset-completion verifier that
    revalidates the host’s current FDT resources and polls the three defined
    SDHCI software-reset bits without issuing a reset write. The host lifecycle
    guard protects each poll from concurrent cleanup. Host and sanitizer
    fixtures cover pending and cleared reset bits, bounded timeout, MMIO
    read failure, invalid limits and cleanup exclusion. Formal G12 milestone
    checks and serial G03/G05 regressions passed at `1a4f31b`; G12 remains
    `IN_PROGRESS`. This is register-model evidence only; no SDIO2 reset is
    issued and no controller activation or hardware behavior is claimed.
  - Power-readiness evidence review found that the pinned Pi 5 tree declares
    `wl_on_reg` as a fixed regulator driven by active-high RP1 GPIO 28, while
    SDHCI power-control bits report controller state only; no rail-voltage
    feedback source is declared. Do not treat a GPIO output latch or the
    controller power register as proof of external 3.3-V rail readiness. The
    WLAN power prerequisite remains unverified and supply activation stays
    gated off.
  - Added a bounded, read-only SDIO2 clock-state verifier. The pinned
    BCM2712 device tree declares the SDIO2 parent as the 200-MHz fixed
    `clk_emmc2`; the verifier revalidates that FDT metadata and polls the
    SDHCI internal-enable, internal-stable and card-clock-enable bits without
    writing controller state. An operation/closing lifecycle prevents host
    cleanup from unmapping MMIO during checks. Register-model and sanitizer
    tests cover invalid bounds, missing/stable-after-poll status, MMIO read
    failure, altered clock metadata and cleanup exclusion. Formal G12
    milestone checks and serial G03/G05 regressions passed at `d412ac1`;
    G12 remains `IN_PROGRESS`. This verifies declared clock metadata and
    controller status only, not a measured physical rate or Pi hardware.
  - Owner-scoped RP1 GPIO 28 output leasing and WLAN supply binding now use
    the exact parsed FDT tree identity as well as the GPIO node index. GPIO
    leases retain ownership after ambiguous writes, exclude raw writes and
    teardown while held, and the GPIO lifecycle blocks new MMIO operations
    while cleanup is closing. Host/ASan/UBSan tests cover lease recovery,
    cleanup exclusion, same-index cross-tree rejection at attach and use, and
    callback suppression on identity mismatch. The focused host suite, formal
    G12 milestone checks, and serial G03/G05 regressions passed at
    `a79b598`; G12 remains `IN_PROGRESS`. This adds a production GPIO lease
    API/adapter, not a production WLAN caller: prerequisite callbacks remain
    fixtures, SDIO2/WLAN activation is disabled, and no physical behavior is
    verified.
  - Host-scoped SDIO2 mapping and gated WLAN supply policy: added an attach
    path that rediscovers the pinned host/function/supply metadata before
    mapping exactly the two SDIO2 host regions. Partial mapping rolls back;
    cleanup is rejected while a WLAN supply lease remains bound. Supply
    control requires an output provider tied to the discovered RP1 GPIO node,
    six bounded read-only prerequisite verifiers (clock, reset, power,
    pinmux, DMA and interrupt route), and the exact 150-ms startup delay.
    Ambiguous output or shutdown failures retain a faulted lease until
    shutdown succeeds. Host and sanitizer fixtures cover owner mismatch,
    stale FDT rejection, partial-map rollback, prerequisite failures, delay
    rollback, output-fault retention and teardown guards. Dynamic review
    found that the GPIO ancestry check accepted a disabled intermediate
    device-tree node; discovery now rejects any disabled node between the
    GPIO provider and RP1, with a regression fixture. Focused host and
    ASan/UBSan tests, formal G12 partial verification, and serial G03 then
    G05 regressions pass at source checkpoint `30205fd`; all 124 entries in
    each G03/G05/G12 SHA-256 manifest match and each result JSON parses.
    G12 remains `IN_PROGRESS`. This is callback-fixture evidence only: there
    is no production GPIO output provider or prerequisite verifier, and no
    MMIO register access, WLAN supply toggle, controller activation,
    firmware, Wi-Fi or hardware behavior is established.
  - Fail-closed BCM2712 SDIO2 WLAN resource discovery: the bounded FDT parser
    now retains SDIO bus width, non-removable status, regulator startup delay,
    and the pinned regulator's singular `gpio` property. Discovery validates
    the translated two-region SDIO2 host, 200-MHz clock, level-high GIC SPI,
    4-bit non-removable bus, WLAN function 1, 3.3-V supply, 150-ms delay, and
    RP1 GPIO 28 ancestry. Fixtures reject mismatched bus width, interrupt,
    WLAN parent, regulator delay, and a disabled intermediate GPIO ancestor.
    The focused host and ASan/UBSan suite
    and formal G12 partial verification passed at source checkpoint
    `04f215f`; G12 remains `IN_PROGRESS`. G03 followed by G05 passed
    serially. All 124 entries in each G03/G05/G12 SHA-256 manifest matched,
    and each result JSON parsed. This is metadata-only discovery: no SDIO2
    mapping, supply toggle, controller activation, CMD5/CCCR, CMD53, WLAN
    firmware, Wi-Fi operation, or hardware behavior is established.
  - Bounded BCM2712 SDIO CMD52 access: added argument validation and encoding
    for function numbers, register addresses, direct reads/writes and
    read-after-write, and rejects all defined R5 error/status bits. Fixtures
    cover the encoded command, returned register data, invalid fields and R5
    errors. This is a protocol primitive only: production SDIO2 clock/reset/
    pinmux/DMA/interrupt providers, CMD5/CCCR initialization, CMD53 data
    transfers, WLAN firmware and controller activation remain unimplemented;
    no Wi-Fi operation or hardware behavior is claimed.
  - Validated post-handoff DMA page-pool mapping: the AArch64 UEFI MMU setup
    now accepts a bounded 64-page LoaderData region only after verifying its
    current translations are page-aligned and physically contiguous, its EFI
    descriptors are LoaderData with consistent normal-memory attributes, and
    each page can be mapped into the new tables. The range is included in the
    live-range install validation. After ExitBootServices and stage-1
    installation, QEMU initializes the page-pool allocator, performs
    owner-checked cache synchronization through the integrated adapter, and
    releases the page. A QEMU fixture temporarily changes the memory-map
    snapshot's pool descriptor type, verifies rejection, restores the
    descriptor, and then completes the valid setup. Failed validation leaves
    active firmware mappings intact. This is UEFI/QEMU software evidence, not
    live controller DMA, RP1 coherency, or Raspberry Pi hardware verification.
  - Owner-matched AArch64 DMA page allocator/cache integration: added a
    fail-closed initializer that binds the generic page-pool allocator only
    to a ready cache provider for the same pool and owner. Host and sanitizer
    fixtures reject a mismatched owner, a different pool, and an uninitialized
    provider, while non-AArch64 cache synchronization remains fail-closed.
    The AArch64 QEMU handoff fixture allocates, synchronizes, and releases a
    UEFI-backed page through the integrated allocator. This establishes
    software allocator/provider wiring only; there is still no production
    runtime allocator caller, descriptor submission, live IRQ/DMA, or physical
    Raspberry Pi verification.
  - Modeled RP1 DMA IRQ route integration: extended the existing PCIe/MSI-X
    fixture with an FDT-backed DMA resource and interrupt node. It exercises
    resource/node mismatch rejection, MSI-X bind rollback, successful
    MSI-X/MIP/GIC callback delivery, retained ownership while a channel is
    claimed, and successful unbind and mapping teardown after release. The
    host and sanitizer suite passed. This is modeled software-path evidence,
    not physical interrupt delivery, production DMA allocation/submission, or
    Raspberry Pi hardware validation.
  - Routed staged DMA completion: an IRQ owner can associate a built stage
    before submission. After every channel status has been read and
    acknowledged, terminal completion updates the stage only when the channel
    owner and transfer sequence still match; callbacks run afterward.
    Mismatched or unacknowledged completion latches a dispatch error and
    retains transfer ownership. Stage detachment is required before discard;
    no DMA hardware activation is added.
  - Fail-closed RP1 DMA IRQ owner and status dispatch: added an FDT-resource-
    matched binding path through the existing RP1 MSI-X/MIP/GIC route. Binding
    requires the mapped DMA resource and matching idle channel pool; the DMA
    mapping cannot be torn down while the route owns it, and unbind refuses
    claimed or in-flight channels. The handler scans and acknowledges all
    channels before delivering any status callbacks; partial acknowledgement,
    invalid status, wrong-vector and reentrant paths fail closed and retain the
    route. Host/ASan/UBSan and AArch64 QEMU fixtures cover modeled dispatch,
    completion/error classification, partial W1C failure, invalid-status
    suppression, and teardown guards. Wrong-vector and reentrant dispatch are
    rejected in code but are not separately fixture-tested. The new PCIe
    fixture now exercises successful modeled FDT-to-MSI-X/MIP/GIC binding and
    callback delivery, plus rollback and idle-only unbind; no production
    caller, active hardware IRQ, descriptor submission or physical behavior
    is claimed.
  - Owner-checked AArch64 DMA cache synchronization provider: added callbacks
    for page-pool DMA allocations. The provider verifies page-base virtual and
    system addresses, pool bounds, configured page ownership, and cache-line
    expansion before issuing `DC CIVAC` before device ownership and `DC IVAC`
    before CPU ownership, with `DSB SY` ordering. The AArch64 QEMU handoff
    fixture executes both instructions on a UEFI-backed page and checks
    misaligned, mismatched-address, and wrong-owner rejection. Host and
    sanitizer tests confirm the non-AArch64 path fails closed. This validates
    software instruction execution and metadata checks only; it does not prove
    RP1 DMA coherency, connect a production runtime caller, or establish
    physical Raspberry Pi behavior. The allocator/cache adapter now has a
    bounded AArch64 QEMU caller, but no live controller operation is enabled.
  - Fail-closed RP1 Audio Out clock-gate lease at source checkpoint
    `af906e8`: validates that the enabled Audio Out node and clock provider
    belong to the same RP1 branch, derives the configured clock rate with the
    gate disabled, and accepts only the pinned 153.6-MHz rate. It preserves
    firmware parent/divider fields, borrows a gate already enabled by another
    owner, and otherwise toggles only the gate-enable bit with bounded
    readback. A failed transition retains the mapped provider and exclusive
    lease when rollback cannot be verified. Release restores only a gate this
    lease enabled and requires Audio Out cleanup to have read back the
    peripheral-disabled state before unmapping. Fixtures cover the 153.6-MHz
    setup, borrowed/new ownership, wrong-rate rejection, teardown rejection
    while active, and injected write/readback failures with retained-state
    retry. `bash scripts/test-pi5-host.sh` passed with ASan/UBSan. The lease
    still has no production caller; the pinned base Audio Out node is disabled,
    no DMA-cache/IRQ production verifier is connected, and neither audio
    operation nor Raspberry Pi 5 hardware behavior is established. Formal G12
    verification remained `IN_PROGRESS`; host/sanitizer tests, x86-64 build,
    and AArch64 QEMU stage-1 installation and non-identity Device-MMIO mapping
    checks passed. GIC target startup remained unverified because QEMU supplied
    no firmware FDT. Required G03 and G05 regressions passed serially; these
    results are recorded in the gate reports and do not accept G12.
  - Fail-closed RP1 Audio Out register lifecycle at source checkpoint
    `3a65fb5`: implemented the pinned 48-kHz stereo S16_LE setup values,
    configuration/start readback and DMA-completion-gated stop. Configuration
    requires the exact enabled owned mapping, the provider-matched claimed
    Audio Out DMA channel and staged descriptor, plus explicit live clock,
    DMA-cache and interrupt verifiers. Start requires the corresponding
    transfer to be in flight; stop requires its completion and only writes the
    pinned zero-sample drain. The upstream controller remains enabled after
    stream stop, so cleanup refuses to unmap it; partial MMIO failures retain
    the mapping fail-closed. Fixtures cover wrong formats/providers/owners,
    missing prerequisites, pre-enabled hardware, register values, write
    failures, transfer-state transitions, readback, drain and teardown guards.
    `bash scripts/test-pi5-host.sh` passed with ASan/UBSan; formal G12 partial
    acceptance and serial G03/G05 regressions passed. No production verifier
    or caller is connected, and no physical audio is claimed. See the pinned
    [RP1 Audio Out driver](https://github.com/raspberrypi/linux/blob/31a34885388835dce2460744d7961f2b9e7dc416/sound/soc/raspberrypi/rp1_aout.c).
  - Transactional RP1 I2S/DMA channel binding at source checkpoint
    `15c6fba`: validated I2S0/I2S1 request targets reserve two distinct
    channels from the matching RP1 DMA provider under one pool lock. Insufficient
    capacity leaves the pool unchanged, and cleanup releases both channels only
    when both remain idle and owner-matched. Fixtures cover both I2S instances,
    provider mismatch, capacity rollback, wrong ownership and staged-channel
    teardown rejection. This reserves software ownership only: no controller
    registers are programmed, DMA is submitted, or audio is produced.
  - Pinned audio-interface audit: the pinned `rp1.dtsi` declares the
    `rp1_audio_out` block disabled and assigns its clock to 153.6 MHz;
    `rp1-common.dtsi` contains no I2S nodes. The pinned `rp1_aout.c` driver
    supports only 48-kHz stereo S16_LE playback and uses a 4-byte FIFO with
    DMA burst 4. Current I2S resource tests therefore establish fixture-level
    support, not availability in the pinned base FDT. No register programming,
    clock enablement, DMA start, or physical audio is established. See the
    [pinned audio-out driver](https://github.com/raspberrypi/linux/blob/31a34885388835dce2460744d7961f2b9e7dc416/sound/soc/raspberrypi/rp1_aout.c),
    [RP1 device tree](https://github.com/raspberrypi/linux/blob/31a34885388835dce2460744d7961f2b9e7dc416/arch/arm64/boot/dts/broadcom/rp1.dtsi),
    and [RP1 common device tree](https://github.com/raspberrypi/linux/blob/31a34885388835dce2460744d7961f2b9e7dc416/arch/arm64/boot/dts/broadcom/rp1-common.dtsi).
  - RP1 peripheral mapper foreign-tree rejection at source checkpoint
    `bb799c8`: audio-out, AXI-DMA, and I2S mapping attachment now reject a
    foreign device tree before invoking the mapper, complementing the existing
    CFE case. `bash scripts/test-pi5-host.sh` passed with ASan/UBSan; formal
    G12 partial acceptance and serial G03/G05 regressions passed. The mappings
    remain inactive and no physical behavior is claimed. A provider audit
    confirmed that GEM clock discovery is read-only and the repository has no
    production GEM reset, clock-enable, power, PHY, or cache-maintenance
    provider; startup remains fail-closed.
  - Fail-closed RP1 GEM prerequisite callback gating at source checkpoint
    `d47d941`: clock/reset/PHY/power/DMA-sync/interrupt checks are no longer
    invoked unless the GEM resources, capabilities, DMA rings, and IRQ binding
    pass structural validation. Invalid structures report the callback
    prerequisites as missing, and fixtures assert zero provider calls for
    missing resources, capabilities, rings, or IRQ binding. Host/ASan/UBSan,
    formal G12 partial acceptance/AArch64 and x86-64 QEMU, and serial G03/G05
    regressions passed. This prevents unsafe provider access but does not add
    production readiness providers or enable GEM. See the
    GEM prerequisite result.
  - RP1 peripheral mapping ownership at source checkpoint `7d3b415`: CFE,
    audio-out, AXI-DMA, and I2S mapping attachment now requires the ready RP1
    PCI function and verifies the mapped FDT node is beneath that function's
    BCM2712 PCIe host before invoking the mapper. Fixtures cover an unready
    function and a foreign tree, both rejected with zero mapping calls, while
    valid owned-tree attachment, rollback, and teardown continue to pass.
    Host/ASan/UBSan, formal G12 partial acceptance/AArch64 and x86-64 QEMU,
    and serial G03/G05 regressions passed. This is software fixture evidence;
    no controller is activated and no physical behavior is claimed. Remaining
    GEM production readiness providers and RP1 DMA submission/IRQ integration
    are still incomplete. See the
    RP1 ownership result.
  - Fixture-only RP1 DMA submitted/completed state and ownership guards at
    source checkpoint `39dc58b`: the channel pool now associates a monotonic
    transfer sequence with the claimed owner and staged descriptor. Completion
    accepts only an acknowledged, valid full-transfer event for that exact
    in-flight sequence; block-only, error, invalid, unacknowledged, unrelated,
    stale-owner/sequence, and corrupted-state cases retain ownership. Timeout
    and failed cleanup likewise leave the transfer owned for retry, and
    descriptor discard is blocked while in flight. Host/ASan/UBSan, formal
    G12 partial acceptance/x86-64/AArch64 QEMU, and serial G03/G05 passed.
    These are fixture state transitions only: no production caller, hardware
    submission, active IRQ, channel activation, or physical behavior is
    established. Production clock/reset/GIC/DMA-cache/I2S readiness is still
    absent. See the DMA completion result.
  - Bounded chained RP1 DMA descriptor construction and owner-checked staging
    at source checkpoint `a4218ac`: added transactional construction for
    transfers spanning up to 16 64-byte-aligned LLIs, with bounded transfer
    counts, validated DMA-range translations, linked-list pointers, and final
    descriptor flags. Staging snapshots prior descriptor bytes, synchronizes
    the complete used chain, rolls back on synchronization failure, and tracks
    the actual chain extent for alias checks. Host tests cover both directions,
    multi-LLI links and terminal flags, over-limit and undersized chains,
    address-space aliasing, sync rollback, discard retry, and ownership.
    `sh scripts/test-pi5-host.sh` passed with ASan/UBSan; formal G12 partial
    acceptance and serial G03/G05 regressions passed on the same source
    revision. No descriptor is submitted, no DMA channel is activated, and no
    completion or physical behavior is claimed. See the
    chained DMA result.
  - Fail-closed HDMI clock/reset/interrupt binding validation at source
    checkpoint `14eac1d`: before mapping any HDMI window, validates the pinned
    firmware HDMI/BVB clock IDs, DVP clock IDs and 108-MHz parent, fixed 27-MHz
    CEC clock, per-port DVP reset specifier, and all five AON interrupt IDs.
    Fixtures verify missing clocks and mismatched clock/reset/interrupt data
    fail without mapping. Focused host/ASan/UBSan, formal G12 partial
    acceptance/x86-64/AArch64 QEMU, and serial G03/G05 regressions passed.
    This validates FDT wiring only: firmware-managed clock state and DVP gate
    state are not queried, resets are not asserted, and HDMI IRQs are not
    bound. No display register access or physical behavior is claimed. See the
    HDMI binding result.
  - Expanded the bounded progressive CTA VIC timing table at source checkpoint
    `6fa09ca`: added VICs 2, 3, 17, 18, 19, and 31-34 from the pinned
    Raspberry Pi Linux `drm_edid.c`, alongside existing VICs 1, 4 and 16.
    Fixtures verify input order, duplicate elimination, unsupported/interlaced
    mode omission, aspect ratios, sync polarity and exact timing fields.
    Focused host/ASan/UBSan, formal G12 partial acceptance/x86-64/AArch64 QEMU,
    and serial G03/G05 regressions passed. HDMI output remains inactive; no
    production readiness or physical display behavior is claimed. See the
    expanded CTA result.
  - Fail-closed DWC3 verifier callback gating at source checkpoint `3b5b660`:
    structural resource, xHCI-capability and ring validation now precede the
    supplied reset/clock/PHY/power/interrupt/event verifiers. If structure is
    invalid, all callback-dependent bits are reported missing and no external
    verifier is called. Fixtures cover malformed resources, non-host state,
    ring corruption, callback failures and the no-write invariant. Host
    ASan/UBSan, formal G12 milestone/x86-64/AArch64 QEMU, and serial G03/G05
    regressions passed. The verifier callbacks remain synthetic; production
    providers and controller activation remain incomplete. See the
    DWC3 callback-gating result.
  - Bounded RP1 DMA IRQ scan and W1C acknowledgement at source checkpoint
    `7a7c124`: reads all eight channel status registers and acknowledges each
    sampled nonzero status, following the pinned DesignWare register map and
    handler order. A processed-channel mask distinguishes completed reads and
    acknowledgements from partial read/write failures. Mock-MMIO tests cover
    pending, block/full completion, error precedence, unknown status bits,
    unrelated channels, undersized mappings and retryable read/ack failures.
    Host/ASan/UBSan, formal G12 milestone/x86-64/AArch64 QEMU, and serial
    G03/G05 regressions passed. The handler is not wired to a live IRQ; no
    descriptor submission or completion ownership release exists, so status
    alone does not establish transfer completion. See the
    DMA IRQ scan result.
  - Pinned RP1 DMA IRQ status decoding at source checkpoint `9e1de74`:
    adds a side-effect-free classifier for known block-transfer, full-transfer,
    pending, error/abort and reserved-bit states, using the
    [pinned DesignWare AXI-DMA header](https://github.com/raspberrypi/linux/blob/31a34885388835dce2460744d7961f2b9e7dc416/drivers/dma/dw-axi-dmac/dw-axi-dmac.h)
    and interrupt flow in its
    [platform driver](https://github.com/raspberrypi/linux/blob/31a34885388835dce2460744d7961f2b9e7dc416/drivers/dma/dw-axi-dmac/dw-axi-dmac-platform.c).
    Fixtures verify completion precedence, error priority and unknown-bit
    rejection. Focused host/ASan/UBSan, formal G12 milestone/x86-64/AArch64
    QEMU checks, and serial G03/G05 regressions passed. The decoder does not
    read or acknowledge hardware interrupts or declare a staged descriptor
    complete. See the DMA IRQ result.
  - Owner-checked RP1 DMA descriptor staging at source checkpoint `3f9349c`:
    the channel pool tracks each staged descriptor, verifies the claimed
    owner/target, rejects descriptor aliasing across channels, and syncs the
    built LLI before marking it staged. Channel release is blocked until
    discard restores and syncs the previous descriptor. Injected sync failures
    verify rollback and retry ownership. Focused host/ASan/UBSan, formal G12
    milestone/x86-64/AArch64 QEMU checks, and serial G03/G05 regressions
    passed. Descriptors remain unsubmitted; completion handling, controller
    activation, I2S pinmux, audio format and streaming remain incomplete. See
    the DMA staging result.
  - Bounded RP1 DMA single-LLI construction at source checkpoint `71d9683`:
    builds unsubmitted DesignWare descriptors for both transfer directions,
    validates controller capabilities, 64-byte descriptor alignment, buffer
    bounds and non-overlap across virtual/system/DMA address spaces, exact
    system-to-DMA translations, transfer-width alignment, channel burst
    settings and the maximum `0x40000` transfer count. Invalid requests leave
    the descriptor unchanged. Focused host/ASan/UBSan, formal G12
    milestone/x86-64/AArch64 QEMU checks, and serial G03/G05 regressions
    passed. Cache synchronization, owner-checked staged lifecycle, completion
    handling and controller activation remain incomplete; no descriptor is
    submitted and no hardware behavior is claimed. See the
    DMA LLI result.
  - Pinned RP1 AXI-DMA capability parsing at source checkpoint `eb87651`:
    bounded FDT parsing and fail-closed discovery now validate the single DMA
    master, 128-bit data-width encoding, all eight `0x40000` block limits and
    the per-channel maximum AXI burst lengths from the pinned `rp1.dtsi`.
    Focused host/ASan/UBSan, formal G12 milestone/x86-64/AArch64 QEMU checks,
    and serial G03/G05 regressions passed. The first G12 attempt exposed an
    ARM64 `__chkstk` link failure after the larger FDT node crossed the stack
    probe threshold; moving the non-reentrant fixture node to static storage
    fixed the test frame, and the complete formal run passed. No descriptor
    submission or hardware operation is implemented. See the
    DMA capability result.
  - Inactive RP1 DMA channel ownership at source checkpoint `64797d5`: a
    bounded eight-channel pool accepts claims only from a validated controller
    mapping, serializes state changes, rejects duplicate or out-of-range
    claims and wrong-owner releases, and refuses cleanup while channels remain
    claimed. Focused host/ASan/UBSan, formal G12 milestone/x86-64/AArch64 QEMU
    checks, and serial G03/G05 regressions passed. The pool does not program
    hardware; no DMA channel is activated. Descriptor staging, cache
    synchronization and completion handling remain incomplete. See the
    channel ownership result.
  - Fail-closed RP1 AXI-DMA controller resource discovery and owned MMIO
    mapping at source checkpoint `c39ee09`: discovery validates the translated
    controller window, exact eight-channel/64-target metadata, DMA/SYS clock
    references, RP1 ancestry and interrupt metadata. Mapping rollback and
    idempotent teardown are fixture-tested, with no controller register access.
    Focused host/ASan/UBSan, formal G12 milestone/x86-64/AArch64 QEMU checks,
    and serial G03/G05 regressions passed. Controller reset, channel operation,
    DMA submission and physical behavior remain unimplemented/unverified. See
    the DMA resource-mapping result.
  - RP1 DMA controller channel-count validation at source checkpoint
    `71ddd58`: bounded FDT parsing now retains the provider's nonzero
    `dma-channels` property, and RP1 audio/I2S discovery requires the pinned
    eight-channel controller count in addition to the DMA target, clock,
    region and IRQ checks. A mismatched count is rejected. Focused host tests,
    ASan/UBSan, formal G12 milestone/x86-64/AArch64 QEMU checks, and serial
    G03/G05 regressions passed. G12 remains `IN_PROGRESS`; this validates
    metadata only and does not implement controller/channel operation or
    establish physical hardware behavior. See the
    DMA channel-count result.
  - Bounded RP1 audio-out/I2S DMA resource discovery at source checkpoint
    `3f27974`: the FDT model resolves bounded `dmas` phandles using provider
    `#dma-cells`; discovery validates the pinned RP1 AXI-DMA window, target
    count, DMA/SYS clock references and interrupt, then validates the exact
    translated audio-out/I2S0/I2S1 windows, clocks and DMA target IDs. Owned
    single-window MMIO mappings roll back and clean up idempotently. Read-only
    `RP1_CLK_DMA` frequency derivation covers the three pinned parents and
    rejects invalid selectors, dividers and parents. Focused host/ASan/UBSan,
    formal G12 milestone/x86-64/AArch64 QEMU checks, and serial G03/G05
    regressions passed; generated JSON and 124-entry SHA-256 manifests
    validated. The earlier test failure was a stale synthetic CSI endpoint
    index after fixture nodes were added; correcting the expected index
    restored the original reciprocal-endpoint assertion without weakening it.
    No clock is enabled, DMA is started, or audio is produced. DMA controller
    operation, audio format/streaming, I2S pinmux, physical audio and hardware
    verification remain incomplete. See the G12 result.
  - Owned RP1 CFE MMIO mapping and teardown at source checkpoint `a6841d7`:
    reuses the established mapper to map the four validated windows, rolls
    back partial failure, and releases mappings exactly once in reverse order.
    Fixtures force map failure at each window, verify rollback and cleanup
    idempotence, and assert mapping makes no register writes. Focused
    host/ASan/UBSan, formal G12 milestone/AArch64 QEMU checks, and serial
    G03/G05 regressions passed. This is resource ownership only; controller
    access, clock/reset/power/pinmux readiness, capture/DMA, and physical
    camera behavior remain unverified. See the
    CFE mapping result.
  - Fail-closed RP1 CFE/camera resource discovery at source checkpoint
    `8ea6af3`: the bounded FDT model now
    resolves reciprocal endpoint phandles and retains CSI clock/data-lane
    metadata. Discovery validates the four translated, non-overlapping CFE
    windows, a single RP1 interrupt, one supported MIPI CFG clock reference,
    and active reciprocal endpoint lane maps before returning resource
    metadata. Focused host/ASan/UBSan, formal G12 milestone and AArch64 QEMU
    FDT checks passed; serial G03/G05 regressions passed. Host tests cover
    malformed resources, clock/IRQ mismatches and endpoint/lane failures.
    This increment does not map CFE registers, enable clocks, start
    capture/DMA, or establish camera hardware operation. See the
    CFE discovery result.
  - Read-only RP1 audio/camera clock rates at source checkpoint `f90625d`:
    adds runtime derivation for `RP1_CLK_AUDIO_OUT`, `RP1_CLK_I2S`,
    `RP1_CLK_MIPI0_CFG` and `RP1_CLK_MIPI1_CFG` from the pinned clock
    descriptors. Checks enabled state, selector/divider bounds, supported
    parents, PLL readiness, secondary divider and maximum rate, and unmaps
    provider resources without modifying clock registers. Focused
    host/ASan/UBSan, formal G12 partial acceptance/x86-64/AArch64 QEMU checks,
    and serial G03/G05 regressions pass. It does not enable clocks or start
    controllers; base audio/CFE nodes are disabled. See the
    clock result.
  - Bounded CTA VIC mode filtering at source checkpoint `aefa5c4`: derives
    timing, aspect ratio and sync polarity for the intentionally supported VIC
    subset 1, 4 and 16 from the pinned Raspberry Pi Linux `drm_edid.c`; removes
    duplicate entries, omits unsupported codes, and rejects malformed input
    without partial output. Focused host/ASan/UBSan, formal G12 partial
    acceptance/x86-64/AArch64 QEMU checks, and serial G03/G05 regressions pass.
    No HDMI programming or physical operation is claimed. See the
    CTA mode result.
  - Bounded DDC first-extension reads and CTA revision-3 parsing at source
    checkpoint `cb19893`: reads the first advertised 128-byte extension at
    offset `0x80`, validates CTA tag/revision/checksum and bounded data/DTD
    regions, captures bounded SVD/VIC codes, and reuses the EDID DTD validator.
    The first host run failed at group 3402; ASan found the test-model command
    lengths array had five entries although the model allowed ten commands.
    The fixture array was corrected without weakening assertions. Focused
    host/ASan/UBSan, formal G12 partial-milestone/x86-64/AArch64 QEMU checks,
    and serial G03/G05 regressions then passed. This does not select/program
    display modes or establish production readiness. See the
    CTA result.
  - Bounded BCM2712 DDC resource ownership and EDID base-block transfers at
    source checkpoint `f8b262c`: validates both translated `0x58`-byte
    `brcm,brcmstb-i2c` windows and the pinned 97.5-kHz bus rate, binds the
    controller only from validated mapped resources, and requires an explicit
    platform-readiness callback before touching controller registers. Polling
    and optional IRQ-wait callback paths cover the repeated-start offset/read
    sequence, four 32-byte FIFO transfers, NACK/timeout failure, ownership,
    rollback and teardown. Focused host/ASan/UBSan, formal G12 partial
    acceptance, and serial G03/G05 regressions pass. The IRQ callback is not
    connected to a production BCM2712 interrupt route, and no production
    readiness verifier or display startup calls this path; the tests use
    register models only. HDMI mode selection/programming and physical display
    operation remain unimplemented/unverified. See the
    DDC result.
  - Bounded EDID base-block parsing at source checkpoint `182911b`:
    validates the EDID header, checksum and supported version/revision, then
    bounds and decodes up to four detailed timing descriptors. Fixtures cover
    a valid 1080p timing, malformed header/checksum/revision/sync bounds,
    short input, and extension-count reporting without claiming extension
    parsing. Focused host/ASan/UBSan, formal G12 partial-milestone/x86-64/
    AArch64 QEMU checks, and serial G03/G05 regressions pass. The parser
    remains a separate API; the new DDC result fixture validates its output,
    but does not establish a production display pipeline. HDMI capability
    remains unimplemented. See the
    EDID result.
  - Bounded BCM2712 HDMI0/HDMI1 MMIO ownership at source checkpoint
    `acb4f05`: validates the pinned compatible, root/simple-bus ancestry,
    translated physical address and size of each of the nine windows. When
    both ports are enabled, the shared DVP window is mapped once and aliased
    without duplicate ownership. Partial-map failure rolls back in reverse
    order; teardown releases each owned mapping once. The FDT fixture models
    the pinned `simple-bus` `ranges` translation. Focused host/ASan/UBSan,
    formal G12 partial-milestone/x86-64/AArch64 QEMU checks, and serial G03/G05
    regressions pass. The tests use mapper models; no HDMI registers are read
    or written, and HDMI capability remains unimplemented. See the
    HDMI MMIO result.
  - Bounded BCM2712 HDMI0 FDT resource parsing at source checkpoint `61a6922`:
    the per-node `reg` limit now preserves all nine translated HDMI0 resource
    windows from the pinned `bcm2712.dtsi`; fixtures verify compatible/status,
    each address/size tuple and the reset specifier. Focused host/ASan/UBSan,
    formal G12 partial-milestone/x86-64/AArch64 QEMU checks, and serial G03/G05
    regressions pass. The first fixture builder hit its 64-byte property cap,
    and the larger FDT node later exposed a test-only AArch64 `__chkstk` link
    failure; both were repaired without weakening tests. This is metadata
    parsing only: HDMI mapping, EDID/mode support and physical display remain
    unimplemented/unverified. See the
    HDMI FDT result.
  - Fixture-tested DHCP ACK lease completion through GEM RX at source
    checkpoint `ce1cafc`: after Discover/OFFER/REQUEST, a checksum-valid ACK
    enters the GEM RX DMA ring and is polled through the shared network stack.
    The fixture verifies the final IP, netmask, gateway, DNS and DHCP-bound
    state, plus RX advancement and no spurious response. Focused
    host/ASan/UBSan, formal G12 partial-milestone/x86-64/AArch64 QEMU checks,
    and serial G03/G05 regressions pass. This does not start GEM or verify
    physical networking. See the
    GEM DHCP ACK result.
  - Fixture-tested DHCP OFFER reception and REQUEST transmission through the
    GEM RX/TX callback paths at source checkpoint `031afa0`: a checksum-valid
    OFFER is injected into the GEM RX DMA ring, processed through the shared
    network-stack poll callback, and validated as a state transition with the
    offered address/server recorded. The resulting DHCP REQUEST is checked in
    the GEM TX ring, including transaction ID and requested-address/server-ID
    options, then reclaimed. Focused host/ASan/UBSan, formal G12
    partial-milestone/x86-64/AArch64 QEMU checks, and serial G03/G05 regressions
    pass. DHCP ACK/lease acquisition, production controller readiness and
    physical network operation remain unverified. See the
    GEM DHCP OFFER result.
  - Fixture-tested DHCP Discover transmission through the RP1 GEM network
    callback adapter at source checkpoint `a9f81e6`: both focused host builds
    now link the shared network stack, initialize it through the GEM adapter,
    submit DHCP Discover through the GEM TX ring, validate Ethernet/IPv4/UDP/
    BOOTP/DHCP fields and reclaim the modeled descriptor. Focused host and
    ASan/UBSan tests, formal G12 partial-milestone/x86-64/AArch64 QEMU checks,
    and serial G03/G05 regressions pass. The first run exposed a fixture-only
    MAC-address offset error; it was corrected and packet validation was
    strengthened. This does not start a production GEM controller or verify
    physical network operation. See the
    GEM DHCP integration result.
  - Bounded xHCI USB storage event-pump bridge at source checkpoint `e5732d6`:
    rings one endpoint once, matches the target slot/endpoint/exact TRB
    completion, forwards unrelated events, and preserves completion/DMA
    ownership across timeouts and event-acknowledgement retries. Event-ring
    fixtures cover matching, dispatch, retry, timeout and teardown. Focused
    host/ASan/UBSan, formal G12 partial-milestone/QEMU checks, and serial
    G03/G05 regressions pass. This does not activate DWC3 or IMAN and does not
    establish live USB behavior. See the
    xHCI event-pump result.
  - Bounded per-slot USB Mass Storage `os_storage_device` adapter at source
    checkpoint `75a2b07`: performs SCSI INQUIRY and READ CAPACITY(10) discovery,
    owns one DMA scratch block, and exposes one-block READ10/WRITE10 callbacks.
    Its bounded event-pump contract passes exact slot/endpoint/TRB completions
    through the BOT consumers, retries bounded completed transport failures
    through BOT recovery, and preserves in-flight ownership on timeout.
    Fixtures cover discovery, callback publication, data copies, bounds, CSW
    failure, recovery, timeout, reentrancy and cleanup. The focused host and
    ASan/UBSan tests, formal G12 partial-milestone/x86-64/AArch64 QEMU checks,
    and serial G03/G05 regressions passed. The first AArch64 build found a
    test-only oversized stack frame requiring `__chkstk`; moving large fixture
    objects to static backing resolved it. Full G12 remains `IN_PROGRESS`.
    There is no production DWC3 event pump, live USB operation or physical Pi
    evidence. See the
    USB storage adapter result.
  - Bounded per-slot USB Mass Storage SCSI INQUIRY and READ CAPACITY(10) at
    source checkpoint `e135d89`: generalized BOT CDB/data submission preserves
    caller-DMA validation and CBW/data/CSW ownership, and queues exact INQUIRY
    and READ CAPACITY(10) wire requests. Parsers require a direct-access
    INQUIRY response and exact eight-byte capacity data; capacity must use
    512-byte blocks, cannot use the READ CAPACITY(10) sentinel last-LBA, and
    must fit the current `os_storage_device` block-count type. Fixtures cover
    both command layouts, valid/malformed responses, capacity arithmetic and
    output preservation on invalid data. Focused host/ASan/UBSan, formal G12
    partial-milestone/x86-64/AArch64 QEMU checks, and serial G03/G05 regressions
    passed. Full G12 remains `IN_PROGRESS`; no live USB or physical Pi behavior
    is established. The `os_storage_device` adapter remains next. See the
    SCSI discovery result.
  - Bounded per-slot USB Mass Storage BOT reset and endpoint-halt recovery at
    source checkpoint `d36e6a0`: issues the class/interface Mass Storage Reset
    through EP0, then clears halt on bulk IN followed by bulk OUT. Completion
    ownership is checked by slot, EP0 endpoint and exact status-stage TRB;
    DMA synchronization failure preserves the pending request for retry.
    Failed control requests restart recovery from reset with at most two
    complete attempts. BOT becomes releasable only after reset and both
    endpoint clears succeed and EP0 is idle. Fixtures cover malformed and
    phase-error CSWs, a stalled data stage, wrong-endpoint events,
    synchronization retry, failed reset/endpoint-clear, bounded attempts and
    teardown guards. Focused host/ASan/UBSan, formal G12 partial-milestone,
    x86-64 build, AArch64 QEMU checks, and serial G03/G05 regressions passed.
    Full G12 remains `IN_PROGRESS`; host mode, IMAN, active DWC3 event/
    doorbell delivery and physical Pi behavior remain unverified. A missing
    completion retains ring ownership rather than canceling an in-flight TD.
    SCSI INQUIRY/READ CAPACITY and storage-device integration remain next.
    See the BOT recovery result.
  - Bounded per-slot USB Mass Storage Bulk-Only Transport (BOT) READ10/WRITE10
    lifecycle at source checkpoint `944073c`: validates the selected
    alternate-zero SCSI-transparent bulk-only interface and its unique
    configured bulk IN/OUT endpoints; constructs exact 31-byte CBWs; serializes
    CBW, bounded data and 13-byte CSW transfers; owns and synchronizes the
    per-slot CBW/CSW and caller-supplied DMA data buffer; and matches completion
    slot, endpoint, TRB and residual before validating CSW signature, tag,
    residue and status. Fixtures cover READ10/WRITE10 directions, CDB fields,
    wrong-slot events, malformed CSWs and stalled data completion. Focused
    G12 host/ASan/UBSan, formal G12 partial-milestone/x86-64/AArch64 QEMU
    checks and serial G03/G05 regressions passed. Stalled, malformed and
    phase-error cases fail closed in `RECOVERY_REQUIRED`; BOT reset/CLEAR_FEATURE
    recovery, SCSI discovery/capacity, storage-device integration and active
    DWC3 event/doorbell delivery remain incomplete. Host mode and IMAN remain
    disabled; no live USB or physical Pi behavior is established. See the
    USB BOT result.
  - Bounded per-slot HID interrupt-IN report lifecycle at source checkpoint
    `5a78238`: after configured selected boot keyboard/mouse endpoints pass
    per-interface SET_PROTOCOL readiness, one exact-length DMA report buffer
    and one Normal TRB are owned per endpoint. Submission validates
    slot/configuration/endpoint state, address width and DMA alias/bounds;
    completion matches slot, endpoint and exact TRB, validates residual length,
    retries after synchronization failure, parses only complete valid reports,
    and permits explicit bounded resubmission. Disable Slot and slot teardown
    reject pending transfers. Fixtures cover concurrent keyboard/mouse
    endpoints, wrong ownership, residual bounds, sync retry, failed/short
    completion, parsing and teardown guards. Focused G12 host/ASan/UBSan,
    formal G12 partial-milestone/QEMU checks, and serial G03/G05 regressions
    pass. This does not connect the event lifecycle to an active controller or
    demonstrate live USB input; host mode and IMAN interrupts remain disabled.
    USB mass-storage BOT now has a fixture-tested READ10/WRITE10 transport
    lifecycle, but recovery, SCSI discovery, storage integration and
    production DWC3 readiness remain incomplete. See the
    HID input result.
  - HID boot-protocol activation at source checkpoint `1d8c682`: submits
    class/interface `SET_PROTOCOL(Boot)` only for selected alternate-zero boot
    keyboard/mouse interfaces after successful `SET_CONFIGURATION`. Completion
    matching is slot/EP0/TRB-specific; failed DMA synchronization retains
    ownership for retry, and failed transfer completion remains retryable.
    Successful completion is recorded per selected interface and gates HID
    interrupt-IN endpoint staging. The first run exposed an incorrect test
    expectation for the setup-packet byte order (`0x00000B21`); correcting the
    assertion to match the USB request encoding resolved it without changing
    acceptance coverage. Focused G12 host/ASan/UBSan, formal G12
    partial-milestone/QEMU checks, and serial G03/G05 regressions passed.
    Keyboard report parsing also accepts repeated HID rollover error usages
    while still rejecting duplicate ordinary key usages. HID interrupt input
    transfers, USB storage, production DWC3 activation and physical operation
    remain incomplete; controller host mode and IMAN interrupts stay disabled.
    See the HID boot protocol result.
  - Bounded HID boot-report parsers at source checkpoint `b4febac`:
    keyboard reports require the exact 8-byte boot format and reject invalid
    or duplicate usages; mouse reports require the exact 3-byte boot format,
    a valid button mask, and decode signed X/Y deltas. Invalid input leaves
    caller output unchanged. Focused G12 host tests and
    AddressSanitizer/UndefinedBehaviorSanitizer passed, followed by the formal
    G12 partial-milestone/QEMU checks and serial G03/G05 regressions. Class
    protocol setup, per-slot interrupt-transfer ownership/retry, and device
    input delivery remain incomplete; controller host mode and IMAN interrupts
    remain disabled. This is software-fixture evidence only, not physical USB
    verification.
  - Bounded xHCI USB `SET_CONFIGURATION` lifecycle at source checkpoint
    `74735d4`: sends the selected configuration value through a no-data EP0
    control TD, retains the exact completion TRB, validates slot/endpoint/event
    ownership, and retries after submit synchronization, failed completion, or
    completion-side synchronization failure. Endpoint staging remains blocked
    until the matching success. Focused host/ASan/UBSan, formal G12 partial
    milestone/QEMU checks, and serial G03/G05 regressions passed. This does
    not activate the controller or establish physical USB behavior. HID class
    binding/input, mass-storage BOT, production DWC3 readiness/activation and
    physical Pi operation remain incomplete. See the
    USB configuration activation result.
  - Bounded xHCI USB configuration-descriptor enumeration at source checkpoint
    `cfb9671`: reads and validates each indexed 9-byte configuration header,
    bounds the `wTotalLength` request to the caller-owned DMA buffer, fetches
    and parses the full descriptor, and advances past valid but unsupported
    configurations. Selection is restricted to alternate-zero HID boot
    keyboard/mouse interfaces with interrupt-IN endpoints and USB mass-storage
    SCSI transparent bulk-only interfaces with both bulk directions. Only
    selected endpoints can be staged after successful enumeration. Exact
    buffer/event identity, synchronization retries, failed-transfer retries,
    and undersized-buffer rejection are covered. Focused host/ASan/UBSan,
    formal G12 partial-milestone/QEMU checks, and serial G03/G05 regressions
    passed. HID class binding, reports, storage I/O, production DWC3
    prerequisites/activation, and physical Pi operation remain incomplete. See the
    USB configuration enumeration result.
  - Bounded USB device-descriptor enumeration at source checkpoint
    `1038d68`: a per-slot state machine submits/consumes the initial 8-byte
    header, validates speed-specific `bMaxPacketSize0`, issues and matches
    Evaluate Context only when EP0 size changes, and then fetches/parses the
    complete device descriptor. Exact descriptor-buffer virtual/system/DMA
    identity and completion-TRB ownership are retained; transfer, command and
    DMA-sync failures preserve retryability. Endpoint staging/configuration
    is blocked until enumeration completes. Tests cover failed header,
    Evaluate Context and full-descriptor completions with retries, the
    unchanged-packet-size path, event/buffer mismatches and successful
    parsing. Focused host/ASan/UBSan, formal G12 software milestone/QEMU
    fixtures, and serial G03/G05 regressions passed. G12 remains
    `IN_PROGRESS`; configuration selection, binding, HID/storage, DWC3
    activation and physical Pi operation are not established. See the
    USB enumeration result.
  - Initial xHCI device-descriptor header request at source checkpoint
    `2818c26`: submits a bounded 8-byte `GET_DESCRIPTOR(Device)` request
    through the published slot's owned EP0 ring, tracks caller-owned DMA
    buffer identity and the exact completion TRB, validates transfer-event
    type/endpoint/slot/address, and preserves pending state on DMA-sync
    failure. A successful short-header completion parses the
    speed-specific `bMaxPacketSize0`; EP0 staging and Evaluate Context remain
    explicit subsequent transitions. Focused host/ASan/UBSan, formal G12
    milestone and AArch64 QEMU software fixtures, and serial G03/G05
    regressions passed. The initial focused failure was an incomplete test
    fixture (`completion_cycle_state` omitted); the fixture was corrected and
    all tests rerun. G12 remains `IN_PROGRESS`; complete enumeration,
    configuration selection, device binding, HID/storage, DWC3 activation
    and physical Pi operation are not established. See the
    descriptor-header result.
  - xHCI EP0 packet-size negotiation at source checkpoint `f0bb51a`:
    validates speed-specific packet sizes, the published DCBAA owner and
    context bounds before changing the EP0 input context. Evaluate Context is
    serialized through the command ring, and only a matching completion can
    commit the new packet size; failed completions retain the staged context
    for retry. Fixtures cover invalid and unpublished state, staging without
    mutation on failure, DMA-sync rollback, failed-completion retry, and
    successful completion. Focused host/ASan/UBSan, formal G12 x86-64 and
    AArch64 QEMU checks, and serial G03/G05 regressions passed. G12 remains
    `IN_PROGRESS`; USB enumeration, HID/storage, controller activation and
    physical Pi operation are not established. See the
    EP0 result.
  - Bounded RP1 xHCI port status and reset at source checkpoint `4d025fc`:
    traverses Supported Protocol capabilities with chain/range bounds, validates
    PORTSC offsets and protocol/speed state, and acknowledges only requested
    W1C change bits while preserving power/indicator/wake controls. USB 2 hot
    reset and USB 3 warm reset require a bounded poll budget and delay callback,
    and complete only after the matching reset-change event and enabled-port
    state. Register-model fixtures cover protocol/range validation, unrelated
    W1C preservation, both reset variants, timeout, invalid bounds, missing
    power and over-current. Host ASan/UBSan, x86-64/AArch64 builds, formal G12
    milestone/QEMU checks, and serial G03/G05 regressions passed. Formal G12
    remains `IN_PROGRESS`; this does not implement enumeration or prove
    physical Pi behavior. See the xHCI port result.
  - Non-control xHCI endpoint configuration at source checkpoint `8c2da81`:
    validates bounded bulk/interrupt endpoint descriptors for Low/Full/High
    Speed, allocates and synchronizes endpoint transfer rings, raises the
    Slot Context Context Entries field, and submits/completes Configure
    Endpoint with retryable failure state. Per-slot doorbells now route EP0
    and configured endpoints using write-only MMIO; DMA-sync and MMIO-write
    rollback are covered. Host ASan/UBSan, formal G12 milestone checks,
    AArch64 QEMU handoff/MMU checks, and serial G03/G05 regressions pass at
    this source checkpoint. G12 remains `IN_PROGRESS`; no device enumeration,
    controller activation or physical Pi operation is established.
  - xHCI Disable Slot teardown at source checkpoint `dc44521`: builds and
    enqueues the pinned type-10 command only for the owning addressed slot,
    matches completion address/type/slot, preserves ownership on failure for
    retry, and clears the DCBAA entry only after successful controller
    completion. Failed CPU/device synchronization leaves the context and DMA
    ownership intact for retry. Fixtures cover command fields, unpublished
    command-ring rejection, enqueue rollback, wrong-slot completion, failed
    completion/retry, successful completion, DCBAA rollback and safe context,
    controller-context and ring teardown. Focused ASan/UBSan, formal G12
    x86-64/AArch64 QEMU milestone checks, and serial G03/G05 regressions pass
    at this source checkpoint; all 123 entries in each G12/G03/G05 SHA-256
    manifest verify. G12 remains `IN_PROGRESS`; this does not establish USB
    enumeration, controller activation or physical Pi operation. See the
    Disable Slot result.
  - Address Device command lifecycle at source checkpoint `dbc6d73`: only
    enqueues against a published command ring with no outstanding command,
    binds the submitted TRB address to its device context, and consumes only
    a matching Address Device completion for the expected slot. Error
    completions release the pending state without freeing published context,
    allowing a safe retry; successful slots remain non-releasable until a
    verified disable-slot lifecycle. Controller-context teardown now rejects
    nonzero DCBAA device entries. Fixtures cover enqueue-sync rollback,
    wrong-slot completion retention, error completion/retry, successful
    completion, and context/DCBAA release guards. Focused ASan/UBSan,
    formal G12 x86-64/AArch64 QEMU milestone checks, and serial G03/G05
    regressions pass. This does not activate DWC3 or establish USB operation.
    See the G12 result.
  - Bounded per-slot xHCI device-context provisioning at source checkpoint
    `61868d9`: allocates 32- or 64-byte input/output contexts and an EP0
    transfer ring, validates slot/port bounds, DMA alignment/ranges and
    buffer disjointness, builds speed-specific EP0 context fields, and
    transactionally publishes the device-context pointer in the DCBAA.
    Rollback and teardown preserve published DMA state when the controller
    publication lock is held. A validated Address Device command initializer
    supplies the input-context DMA address and slot ID. Fixtures cover 32/64
    byte layouts, low/full/high/SuperSpeed packet sizes, occupied slots,
    publication-sync rollback, command fields and locked teardown. Focused
    ASan/UBSan, formal G12 x86-64/AArch64 QEMU milestone checks, and serial
    G03/G05 regressions pass. Address Device submission/completion, endpoint
    doorbells, USB enumeration, HID/storage and production DWC3 readiness
    remain incomplete; controller host mode and interrupts remain disabled.
    See the G12 result.
  - Bounded xHCI control-transfer TD construction and completion at source
    checkpoint `e55da34`: emits Setup/Data/Status stages (or Setup/Status for
    no-data requests), requires an active owned DMA buffer for data stages,
    synchronizes it before ring publication, records TD boundaries for FIFO
    completion, and restores ring entries and Link TRB state after failed
    synchronization. Fixtures cover IN/OUT and no-data requests, wraparound,
    complete-TD retirement, invalid buffer bounds, data/ring synchronization
    failures and rollback. G12 host/ASan/UBSan, x86-64 build, AArch64 QEMU,
    and serial G03/G05 regressions pass at this source checkpoint. This is
    software-only TD construction; device enumeration, HID/storage and
    production DWC3 activation remain incomplete. See the control-transfer
    result.
  - xHCI Enable Slot command construction and completion-result decoding at
    source checkpoint `915196e`: builds the pinned type-9 command and retains
    command type, nonzero completion code and returned slot ID from a matching
    command-completion event. Invalid completion codes and malformed ring
    state are rejected without retiring a command. Host/ASan/UBSan, G12
    x86-64/AArch64 QEMU milestone checks, and serial G03/G05 regressions pass.
    Pinned Linux confirms that slot allocation precedes device-context
    allocation, Address Device uses an input-context pointer plus slot ID, and
    transfer doorbells are per-slot/per-endpoint. Device contexts, Address
    Device submission, endpoint doorbells and USB enumeration remain
    incomplete. See the Enable Slot result.
  - Bounded USB device/configuration descriptor parsing and xHCI transfer-ring
    lifecycle at source checkpoint `9c08d17`: the ring accepts only bounded
    single Normal-TRB, unchained submissions, tracks FIFO completion and cycle
    state, prevents release with outstanding transfers, and restores Link TRB
    state after a failed DMA synchronization. Descriptor parsers validate
    lengths, declared counts, endpoint bounds and endpoint-zero packet-size
    encodings with transactional output. Host/ASan/UBSan, x86-64/AArch64
    builds, G12 milestone/QEMU and serial G03/G05 regressions pass. USB control
    TDs, enumeration, HID/storage and production DWC3 activation remain
    incomplete. See the xHCI/USB result.
  - Prerequisite-gated GEM engine lifecycle and shared network callback
    integration at source checkpoint `c98823e`: controller startup validates
    queue publication, readiness callbacks and a stopped engine before
    enabling GEM IRQs and RX/TX; shutdown masks sources, restores queue bases
    and tears down the route. TX callbacks submit/reclaim and issue NCR.TSTART;
    RX callbacks poll and rearm descriptors. Modeled IER/NCR failures, host
    sanitizers, both architecture builds, G07, G12 milestone/QEMU and serial
    G03/G05 pass. Production clock/reset/power/PHY/DMA providers and physical
    Ethernet remain unverified. See the GEM network result.
  - Prerequisite-gated RP1 GEM queue-0 DMA-base publication at source
    checkpoint `c83178e`: after read-only clock/reset/PHY/power/DMA/route
    checks, it requires the GEM engines stopped, validates descriptor-base
    alignment and 32/44-bit bounds, saves existing queue registers, publishes
    44-bit high words before low words with readback, and rolls back partial
    writes. An incomplete rollback remains explicitly locked and retryable.
    Host/ASan/UBSan, G12 milestone/QEMU and serial G03/G05 regressions pass.
    The implementation does not start GEM, kick queues or prove physical
    Ethernet. See the queue publication result.
  - RP1 GEM interrupt lifecycle, committed at `5db7eb2` and hardened at
    `f8a44fa`: discovers clear-on-write versus read-clear behavior from DCFG1,
    binds the MSI-X/MIP/GIC route with GEM sources masked, and permits source
    enable only after bounded clock/reset/PHY/power/DMA/IRQ readiness checks.
    Rollback and retry paths are modeled. Host/ASan/UBSan, AArch64 build/QEMU,
    full G12 milestone and serial G03/G05 checks pass at `4c44cd5`. This is not
    physical interrupt-delivery evidence.
  - Bounded network-device API at source checkpoint `c5c429e`: exposes
    callback-backed stack initialization, bounded receive polling and DHCP
    start; validates unicast MAC addresses and alias-safe initialization.
    Host/ASan/UBSan, x86-64/AArch64 builds, G07 host/QEMU protocol tests,
    G12 milestone checks and serial G03/G05 regressions pass at source
    checkpoint `4c44cd5`. This is not a GEM hardware adapter or physical
    network operation.
  - The earlier inactive GEM DMA-ring and packet-transition fixtures remain
    software-only. No GEM registers are programmed and the PHY reset is not
    actuated. The model does not prove the actual hardware DMA address width.
  The preceding bounded RP1 GEM resource attachment:
  Attachment validates the RP1 ancestry, translated MMIO resource, exact
  pinned four-clock specifiers and read-only rates, IRQ, PHY/reset metadata
  and nonzero unicast MAC before mapping GEM registers. It preserves the
  active-low PHY reset information without actuating it, rolls back failed
  mapping, and has cleanup fixtures. See
  the GEM resource result at source
  checkpoint `d31e95c`. This is not GEM initialization, DMA, packet I/O or
  physical Ethernet operation.
  The preceding read-only RP1 Ethernet clock-rate increment is recorded in
  the Ethernet clock result at
  source checkpoint `c608a2f`; it does not initialize GEM.
  The preceding fail-closed RP1 DWC3 external-dependency declaration increment
  is recorded in
  the dependency result at source
  checkpoint `84fb242`. Device-tree parsing preserves PHY, power-domain and
  VBUS-supply references, and DWC3 attachment rejects those plus clock/reset
  references before mapping; this is a configuration guard, not a runtime
  clock/PHY/power readiness check.
  The preceding xHCI scratchpad context and halted-only register publication
  increment is recorded in
  the publication result at source
  checkpoint `3909d90785c30b4b6ad2ba36c247060e3dd962e2`.
  The preceding DCBAA-only increment is recorded in
  the context result at source
  checkpoint `c043b48e701a33f1d93322e921d0bdfe81a631aa`.
  The preceding increment modeled xHCI event dispatch through RP1 MSI-X/MIP/GIC,
  including bind rollback and unbind restoration; the source checkpoint is
  `b9872775d1ef3fb19d6cdabb0f4f1342a196f13e`. Verification evidence is
  recorded in the route result.
  The preceding fail-closed MSI-X callback adapter is recorded in
  its result at source
  checkpoint `761465f6d9ee0de428b2de3d4b052b55aab716a7`. The preceding bounded
  event-dispatch increment is recorded in
  its result at source
  checkpoint `c6f2be1d8dda9a1cceeeb65b82a435c5764bece6`. Earlier bounded ERDP
  and IMAN.IP acknowledgement is recorded in
  its result at source
  checkpoint `ad4d90fccb7479cc22d4623f6206cc7470933820`.
  The preceding command-completion event decoder is recorded in
  its result at source
  checkpoint `fd838ef402b82f2b01ebfb57eb42bfcc0a9d1597`. The earlier bounded
  ERDP acknowledgement is recorded in
  its result at source checkpoint
  `6238d7ea385f8506a14047e76f6ecdb828011a3a`. The earlier DWC3 startup
  prerequisite validation over the xHCI
  command/event ring software state and transfer-ring allocation is recorded
  in its result and
  the xHCI ring-state result
  and transfer-ring result.
  The earlier
  xHCI ring allocation result remains
  available.
  The prior DWC3 core identity validation
  remains recorded at source checkpoint `f23fe7d48cef8e1456540f12c0a078fa4483f79d`.
  The prior xHCI capability and register-range validation
  remains recorded at source checkpoint `e7abdc27f4f17ed77174a07e3b2704d1dbee921a`.
  The prior RP1 DWC3 host-role policy validation
  remains recorded at source checkpoint `bb8f533b1defcc045991818bf65396a36cf4afdf`.
  The prior reset and bounded clock milestone
  remains recorded at source checkpoint `0cde328`.
  The prior 400 kHz initialization-clock milestone
  remains recorded at source checkpoint `e68914a`.
  The prior SDHCI resource discovery
  remains recorded at source checkpoint `4d19ac937f1df005cbbab4e50d9e4ff02211e3ac`.
  The prior RP1 DWC3 resource lifecycle
  remains recorded at source checkpoint `71c14a921c0d32bd432e4f1bc5dc49a505281c22`.
  Earlier BCM2712 clock-policy and RP1 runtime clock increments are recorded
  in their evidence and
  their evidence. The guarded AArch64 MMU/GIC
  milestone remains recorded in
  its evidence.
- The owner-approved native ARM64 GitHub Actions workflow is local at
  `.github/workflows/native-arm64.yml`. Workflow syntax/security checks and
  serial host regressions passed locally under x86-64 WSL2. It has not run on
  GitHub; no repository remote is configured, so repository visibility and
  remote execution are unknown. This does not change any gate status.

## Implemented and software-tested

- Bounded UEFI configuration-table lookup for the standard FDT table GUID and
  FDT v16/v17 parsing, including multi-compatible matching, 3-cell PCI address
  translation of `assigned-addresses` (while retaining PCI config `reg`
  metadata), multiple register regions, interrupt-parent/phandle resolution,
  clock/reset provider-specifier resolution, bounded DMA-range metadata and
  DMA address translation with range checks and malformed-input fixtures.
  DesignWare I2C SCL rising/falling time properties are preserved with strict
  nonzero and duplicate checks.
- Bounded MMIO callback interfaces, an opt-in direct-MMIO adapter, and a
  mapper-backed lifecycle that accepts only translated, size-bounded regions
  and rolls back partial mappings. Platform mapping callbacks remain
  responsible for page attributes and hardware permissions.
- GIC-400 three-cell interrupt resources are decoded into bounded GICv2
  interrupt IDs and trigger flags; only the standard `arm,gic-400` binding is
  accepted by this helper. Fixed-clock providers expose their declared
  nonzero frequency. Both paths have FDT fixture coverage.
- Added GICv2 SPI registration, Group-1 routing, priority and target setup,
  enable/disable, IAR dispatch/EOI, spurious and unhandled handling, and cleanup
  that restores pre-registration and Group-1 interface state. A platform
  lifecycle now resolves a unique enabled GIC-400 node, maps only its
  distributor and CPU-interface regions through a trusted mapper, and supports
  explicit start, stop and cleanup. The AArch64 IRQ vector saves/restores
  general registers and enters the dispatcher. Target startup now maps GIC
  regions only through the installed FDT-authorized MMIO mapper, discovers the
  executing CPU target from GICD_ITARGETSR bytes, and keeps CPU IRQs masked
  during activation. Host fixtures test target discovery and rollback. QEMU's
  UEFI exposes no FDT, so it does not exercise or claim target GIC startup or
  live Pi IRQ delivery.
- DMA buffers can be allocated from the kernel page pool with bounded
  contiguous-page allocation, alignment, zeroing, ownership and physical
  addresses. Device-specific DMA ranges are required for translation, and
  explicit cache-sync callbacks are required for CPU/device handoff. Allocation,
  translation, synchronization, failure cleanup and release are fixture-tested;
  no Pi peripheral currently consumes these buffers.
- Bounded PCI ECAM function scanning across an explicit bus range, including
  multifunction devices, plus unique RP1 C0 identity matching using the Linux
  IDs `1de4:0001`. Host register models and the AArch64 guest test the scan,
  capacity/error behavior and identity matching.
- Added BCM2712 host attachment to a validated, enabled
  `brcm,bcm2712-pcie` Device Tree node, trusted-mapper lifecycle and rollback,
  root-complex/link-state checks, aligned/bounded indexed downstream config
  reads and writes, and bounded root/downstream function enumeration. The
  validated Device Tree memory ranges are programmed into BCM2712 outbound
  windows with register snapshots and rollback on a failed write. Host and
  AArch64 register-model tests cover 32/64-bit windows, RP1 identity,
  multifunction devices, malformed resources, duplicate controllers, failed
  mapping and cleanup. A separate staged startup path now resolves the BCM2712
  RESCAL and bridge reset phandles, performs the documented bridge deassert and
  bounded RESCAL sequence, applies the pinned Linux BCM2712 MDIO/PHY post-setup
  writes, deasserts active-low PERST through the verified `0x4064` control bit,
  waits the documented 100 ms before bounded link polling, and reasserts PERST
  if startup fails after attempting deassertion. Startup failure also asserts
  the bridge reset and releases mapped resources. Fixtures cover reset register
  semantics, MDIO values, controller post-setup fields, timeouts and startup
  rollback. PERST fixtures cover read-modify-write bit preservation, invalid
  bounds and read/write failures. The staged-startup fixture retains its
  intentional MDIO-timeout rollback path and now also models MDIO completion to
  exercise successful startup through PERST release, link detection and
  controller mapping cleanup. Separate start-link fixtures cover PERST
  release, the 100 ms delay, bounded successful link detection, timeout and
  rollback. This validates software sequencing only; physical root-complex
  startup remains unverified. Audit of the pinned BCM2712 base DTS shows no
  PCIe `clocks` or `power-domains` property; it declares the RESCAL and bridge
  resets already handled here. The pinned Linux PCIe driver requests an
  optional `sw_pcie` clock, absent from these nodes. Startup rejects any
  nonzero clock-reference count before mapping rather than guessing how to
  enable an unprovided clock. Optional-clock overlays are unsupported. The subsequent
  controller-setup increment programs
  validated inbound DMA windows transactionally and applies the verified
  host-setup registers. A follow-up correction places MDIO WR_DATA at `0x1104`
  and records RD_DATA at `0x1108`, matching the pinned Linux register map; the
  host test asserts those exact offsets. Endpoint BAR probing now handles
  32-bit and 64-bit memory BARs, I/O BARs and unimplemented BARs, with decode
  and bus mastering disabled during the probe and original configuration
  restored on success or probe failure. Host and AArch64 fixtures test masks,
  sizes, attributes, restoration and an injected write failure. This remains
  partial controller bring-up: physical root-complex startup validation is
  not implemented. A bounded
  first-fit PCI resource allocator imports and
  validates non-overlapping memory windows from FDT `ranges`, reserves
  preassigned BARs, allocates aligned memory resources and tracks reservations
  with generation-checked release handles. The assignment lifecycle now
  programs unassigned BARs with decode/bus mastering disabled, verifies
  readback, maps through a trusted mapper, and retains rollback state until
  explicit teardown; preassigned BARs can be reserved and mapped without
  reprogramming. Host and AArch64 fixtures cover assignment, map failure,
  configuration-write failure, recovery, release and preservation of
  preassigned configuration. RP1 attachment now requires a unique `1de4:0001`
  function and a BAR1 large enough to contain the pinned Linux APBS/MSI-X
  register range, then maps and owns that BAR with explicit cleanup. The
  pinned driver's 61 vectors and register window are recorded; bounded
  capability discovery, APBS MSI-X set/clear operations, endpoint MSI-X table
  programming and MIP/GIC dispatch have since been integrated and
  fixture-tested. Binding vectors to concrete RP1 child drivers remains
  outstanding. Nodes declaring a
  clock reference are rejected because
  no clock-enable service exists; the Pi 5 `pcie2` node at pinned revision
  `31a34885388835dce2460744d7961f2b9e7dc416` has no clock property. No
  physical hardware behavior is claimed.
- BCM2712 MIP MSI controller FDT references and `msi-ranges` are resolved.
  The MIP registers are mapped and initialized using the pinned Linux register
  layout; message targets are validated through PCIe DMA ranges; and vector
  allocation/release is connected to GICv2 registration and dispatch. Tests
  cover initialization, mapping and registration failures, dispatch, stale
  handles, and register restoration.
- Bounded PCI capability-list traversal extracts MSI-X table size, table BIR
  and offset, and PBA BIR and offset. RP1 attachment requires at least the 61
  vectors used by the pinned Linux driver. Malformed pointers, cycles,
  duplicates, invalid BAR indices and absent capability-list status are
  rejected. MSI-X table and PBA bounds are checked against acquired BAR
  mappings. The driver masks/restores MSI-X function and vector state,
  programs allocated MIP message address/data, enables/disables RP1 APBS
  sources, dispatches handlers through the existing MIP/GIC route and issues
  APBS IACK for level-triggered callbacks. Unbind and cleanup restore table
  entries, release vectors and preserve retryable state after incomplete
  rollback. Host and AArch64 register-model fixtures cover table/PBA bounds,
  vector activation/deactivation, edge and level dispatch/acknowledgement,
  injected failures and resource rollback. GPIO, UART, SPI and I2C child
  drivers bind their discovered interrupts; fixtures do not establish
  physical MSI-X delivery.
- RP1 two-cell interrupt-controller resources are decoded with a strict
  0-60 vector bound and validated trigger flags. The MSI-X child-binding API
  accepts only enabled device-tree nodes beneath the attached BCM2712 host,
  verifies the enumerated RP1 PCI identity, resolves the node's ancestor
  interrupt controller, and binds its vector to a kernel handler. Synthetic
  host and AArch64 fixtures test valid level/edge values, malformed vectors
  and rejection of nodes outside the attached host. The API is wired to the
  GPIO, UART, SPI and I2C child-driver handlers.
- RP1 GPIO input/output/value operations using the documented 54-pin bank
  grouping, with bounds, register-model and failure-path fixtures.
- RP1 GPIO can now attach only from an enabled child node beneath the attached
  BCM2712 host and enumerated RP1 identity. It maps the three translated GPIO
  regions through the trusted MMIO mapper, initializes the existing controller
  API, and releases partial mappings on failure. Host fixtures exercise
  out-of-host rejection, a failed second-region mapping, GPIO output, and
  teardown. This is a software mapper fixture, not physical GPIO verification.
- RP1 GPIO now binds its three bank parent interrupts through the existing
  host-scoped MSI-X/MIP/GIC lifecycle. It configures validated rising, falling,
  both-edge, high-level and low-level events; enables/disables each bank pin;
  clears pending pin events before handler dispatch; acknowledges the parent;
  and rolls back partial vector binding and cleanup. Fixtures cover all three
  banks, pending/unconfigured pins, APBS acknowledgement, failed binding and
  teardown. This does not establish physical interrupt delivery.
- PL011 UART initialization, bounded TX polling and RX handling, with
  register-model and direct-MMIO bounds fixtures.
- Device-tree parsing now preserves the zero-length `skip-init` flag. For the
  pinned RP1 `arm,pl011-axi` nodes, the PL011 driver can adopt an already
  enabled UART without writing its registers or requiring caller-supplied clock
  and baud values; it rejects disabled UART state. This avoids disrupting
  firmware configuration, but is not yet a full RP1 UART resource, clock or
  interrupt integration.
- RP1 UART attachment now requires an enabled child of the enumerated RP1
  under the attached BCM2712 host, a translated `arm,pl011-axi` register
  resource, and `skip-init`. It maps the bounded region with the trusted MMIO
  mapper, adopts the already enabled controller without writes, and unmaps on
  cleanup or attach failure. Host fixtures cover foreign-node/resource/mapping
  rejection, reuse prevention, UART I/O, state preservation and teardown.
- RP1 UART receive interrupts now bind the UART node's validated interrupt
  vector through MSI-X/MIP/GIC, enable only RX/receive-timeout masks, drain the
  PL011 receive FIFO with a fixed bound, and deliver received bytes to the
  registered callback. Disable and cleanup mask and clear the UART source,
  restore the MSI-X table state, release the vector and unmap only after
  successful teardown. Host fixtures exercise delivery, duplicate enable,
  cleanup failure/retry and full resource release; this remains synthetic
  software verification.
- DesignWare SPI and I2C now have host-scoped RP1 child attach/cleanup APIs.
  They validate the enumerated RP1 ancestry, compatible and translated MMIO
  region before using the existing register drivers; cleanup disables the
  controller before releasing its mapping. Fixtures cover attached-region
  lifecycle and register setup. I2C HCNT/LCNT values are derived from the FDT
  SCL rise/fall properties and bus rates. A read-only
  `raspberrypi,rp1-clocks` provider path now derives `RP1_CLK_SYS` from the
  fixed XOSC parent and live PLL feedback/primary divider, source mux and SYS
  divider registers. Clocked SPI/I2C attachment APIs use that rate rather than
  assigned-clock metadata or a caller-supplied synthetic input rate. The
  provider rejects disabled clocks, unlocked/powered-down PLL state, malformed
  selectors and unsupported clock IDs; it never writes clock registers.
  SPI attachment derives the smallest valid even BAUDR divisor that respects
  an explicit SCLK ceiling. Read-only rate discovery now also supports
  `RP1_CLK_ETH` and `RP1_CLK_ETH_TSU`, based on their verified parent muxes,
  PLL state and divider bounds. USBH0/USBH1 clock IDs remain unsupported:
  although the pinned provider defines their registers, they have no
  registered clock descriptors, so their rates must not be inferred.
  Clock enable/power lifecycle remains incomplete.
- DesignWare SPI/I2C bind their RP1 child interrupt specifiers through the
  host-scoped MSI-X/MIP/GIC route. SPI error status and I2C overflow/abort/STOP
  status are cleared and delivered to registered callbacks; failed status
  reads latch dispatch errors, and active polled transfers fail rather than
  hiding those errors. Register-model fixtures cover normal delivery,
  clear/read failure, transfer-failure propagation and cleanup. Transfers
  remain polling-based, and no physical interrupt delivery is established.
- RP1 DWC3 resources now have an explicit attach/cleanup lifecycle. It accepts
  only an enabled `snps,dwc3` RP1 child with the pinned 1 MiB translated region
  and a validated single RP1 interrupt, then maps the resource through the
  trusted mapper. The FDT parser preserves `dr_mode`; attachment requires the
  pinned `host` role and edge-rising RP1 interrupt. Fixtures cover missing or
  unsupported roles, wrong trigger, foreign-node rejection, successful map
  and cleanup, and mapping failure. It also rejects declared clock, reset,
  PHY, power-domain and VBUS-supply dependencies because no production providers
  exist. This does not prove runtime readiness or initialize DWC3, bind its
  interrupt, connect the separately allocated inactive DMA rings to hardware,
  or provide USB host functionality.
- The new read-only xHCI probe first requires DWC3 PRTCAP to report host mode,
  then validates xHCI version/capability length, slot/interrupter/port counts,
  supported 4-KiB pages, extended-capability pointer bounds, and the
  operational, doorbell and runtime register ranges for alignment, bounds and
  overlap. Fixtures cover malformed values and failed reads. It neither writes
  DWC3/xHCI registers nor activates an interrupt. Inactive event/transfer ring
  resources and software ring-state helpers exist, but controller initialization,
  hardware ring publication, interrupts and USB functionality remain
  unimplemented.
- Before capability parsing, the DWC3 identity probe accepts only the Synopsys
  core IDs recognized by the pinned Linux driver and uses the separate
  version-number/type registers for DWC31/DWC32 (including the DWC4 alias).
  Unsupported IDs and failed reads are rejected without writes. This is
  identity validation only; it does not establish clocks, reset, PHY readiness
  or controller operation.
- A read-only startup prerequisite gate revalidates the mapped DWC3 resource,
  bounded RP1 interrupt, core/xHCI identity and register capabilities, DMA ring
  ownership/alignment/address bounds, and ERST descriptor before considering
  startup. It requires bounded verification callbacks for reset, clock, PHY,
  power, interrupt routing and the complete event lifecycle, and reports each
  missing dependency. Invalid resources, unsupported host state, malformed
  rings, unavailable callbacks, failed checks and zero polling bounds fail
  closed. Register-model tests cover all six independent dependency failures
  and verify no MMIO writes occur. These callbacks are a verifier contract, not
  production reset/clock/PHY/power providers. A new read-only runtime verifier
  now checks the bound MSI-X enable/mask and table message, MIP vector
  generation, GIC registration/interface, event-dispatch state and a fresh
  xHCI capability read; it reruns structural and caller-supplied platform
  checks. The config-space read may write the PCI host's config-window selector,
  but does not write DWC3/xHCI registers, the MSI-X table or endpoint
  configuration. Integrated RP1 USBH clocks remain unsupported, and runtime
  clock/PHY/power state, hardware ring publication, live event delivery and
  controller activation remain unverified. This verifier does not authorize
  controller activation in the current platform.
- Bounded xHCI DMA resources now allocate one 256-TRB command segment (with a
  Link TRB), one event segment and a one-entry event segment table through the
  existing DMA-range allocator. Segment alignment, AC64 address limits and
  virtual/DMA overlap are checked; memory is initialized and synchronized
  before publication. Partial allocation/synchronization failures roll back,
  and teardown failures preserve ownership for retry. These resources are not
  programmed into the controller. Software command enqueue/completion tracking
  and event-ring consumption now validate ring ownership, queue capacity,
  producer/consumer indices, cycle bits and DMA synchronization. The tests
  cover full-ring rejection, wraparound, out-of-order completion rejection,
  empty events, sync failure and teardown while commands are pending. A
  command-completion event consumer accepts only TRB type 33 and requires the
  event's command DMA address to match the oldest pending command before it
  advances software completion state. Wrong event types and addresses leave
  the pending queue unchanged. A
  bounded ERDP acknowledgement helper validates the current event-ring
  dequeue address, writes the 64-bit pointer low-dword first then high-dword,
  and verifies the returned pointer. It then writes IMAN.IP as write-one-to-clear
  while preserving IMAN.IE and reads the register back to flush the posted
  acknowledgement, following the pinned Linux sequence. A bounded dispatcher
  now routes command-completion events through the pending-command validator
  and other events through a caller-supplied handler. Callback failure restores
  the software event cursor; successful events are acknowledged immediately,
  with pending acknowledgement retried before another event can be consumed.
  `MORE` indicates the event budget was exhausted and requires another call.
  Ring teardown and DWC3 startup prerequisites reject pending acknowledgement.
  Register-model fixtures cover callback failure, command completion, budget
  exhaustion, acknowledgement rollback/retry, MMIO bounds and teardown guards.
  A fail-closed context initializer now reruns the read-only capability and
  startup prerequisite checks before exposing a callback compatible with the
  existing MSI-X handler. The callback validates the delivered vector, handles
  one bounded batch, records follow-up work/errors, and leaves configured APBS
  acknowledgement to the existing MSI-X wrapper. Host fixtures now exercise
  the callback over the modeled MSI-X/MIP/GIC route and the runtime verifier
  checks that complete software binding. No production DWC3 driver installs
  it; it receives no hardware events, does not update xHCI doorbells or enable
  IMAN interrupts, and does not provide live USB functionality.
- A separate per-transfer xHCI ring allocator now prepares one aligned
  256-entry segment with its Link TRB, validates address width and segment
  bounds, synchronizes initialized memory, rejects duplicate allocation, and
  preserves ownership when cleanup fails so release can be retried. Fixtures
  cover initialization, alignment, address-width rejection, synchronization
  rollback and cleanup retry. No endpoint context is created, no transfer is
  submitted, and the ring is not exposed to the controller.
- BCM2712 SDHCI resource discovery now validates the pinned `brcm,bcm2712-sdhci`
  node, its two translated register regions (`host` 0x260 bytes and `cfg`
  0x200 bytes), one fixed 200 MHz clock, and the level-high GIC SPI 273
  interrupt. Attach/cleanup maps and releases both regions through the trusted
  mapper, with fixtures for invalid clock/interrupt metadata and rollback when
  the second mapping fails. Exact-width 8-/16-bit MMIO callbacks now support
  the SDHCI register interface. A bounded reset-all operation follows the
  pinned SDHCI core's 10-us delay/100-ms timeout behavior; it reports register,
  delay and timeout failures without claiming reset completion. A
  guarded 3.3-V power path refuses pre-existing controller power, verifies
  the SDHCI power bits, waits the board's documented 5-ms regulator settling
  time, and retains ownership if shutdown fails. Bounded FDT support validates
  the Pi 5 `vmmc-supply`, `vqmmc-supply`, `cd-gpios`, AON GPIO bank widths,
  GPIO polarities, rail voltages and regulator states. The mapped AON GPIO
  provider drives GPIO 3 to the documented 3.3-V selection and GPIO 4 to
  enable/disable card power, while preserving/restoring owned bits; GPIO 5
  provides active-low card presence. Only the validated 3.3-V mode is exposed.
  These callback-driven register fixtures do not prove physical rail, pinmux,
  or card behavior. A bounded, rollback-aware clock path selects the
  documented BCM2712 SD mode, calculates a spec-version-aware divisor, starts
  the internal clock, polls stability within a caller-supplied bound, and
  enables the 400 kHz initialization card clock only after reset and power.
  A bounded command/response primitive checks command/data inhibit state,
  response/error completion, stale-status clearing, response capture and
  bounded timeouts. SDHC identification exercises CMD0/CMD8, ACMD41 polling,
  CID/RCA/CSD acquisition and card selection, and rejects unsupported legacy
  or standard-capacity cards rather than guessing their addressing mode.
  Register-model fixtures cover GPIO mapping, card-presence polarity, rail
  rollback/restoration, command errors/timeouts, and the identification
  sequence. CSD v2 capacity decoding rejects values outside the block API's
  range. Single-block CMD17/CMD24 PIO transfers now attach through
  `os_storage_device`; fixtures verify FIFO byte order, block bounds, callback
  dispatch, status/error timeout handling, data-line reset and register
  restoration. These are software models, not physical card tests. DMA,
  high-speed modes and physical storage behavior remain unverified.
- RP1 SPI attachment rejects invalid/unrepresentable clock rates before MMIO
  mapping. Host fixtures cover even-divisor rounding, the minimum divider,
  register-width bounds and mapping-free failure. Clock-provider fixtures
  compute 200 MHz from modeled live PLL/divider registers, test the XOSC path,
  and exercise clocked SPI/I2C attachment and cleanup. These register models
  are not Pi 5 measurements.
- DesignWare SSI/SPI bounded full-duplex transfers, chip-select/mode validation,
  polling timeout and cleanup, with register-model fixtures.
- DesignWare I2C standard/fast-mode controller setup and bounded master
  write/read transactions, including repeated-start, final-stop, abort,
  timeout and cleanup paths, with register-model fixtures. SCL high/low counts
  derive from parsed FDT edge times and explicit clock/bus rates; the new
  clocked attachment API supplies the read-only measured `RP1_CLK_SYS` rate.
- Added a bounded four-level AArch64 stage-1 page-table constructor for 4-KiB
  granules, including 1-GiB/2-MiB blocks, 48-bit virtual-address validation,
  configured physical-address bounds, device and normal-memory attribute
  indices, execute/read-only flags, translation, exact-range unmap, overlap
  rejection and rollback on table-pool exhaustion. The UEFI handoff now
  validates the final memory map and active code, stack, vector, image and
  page-table translations before installing MAIR/TCR/TTBR with rollback and
  TLB maintenance. A bounded non-identity Device-MMIO window authorizes only
  translated FDT `reg`/`assigned-addresses` resources, tracks exact mapping
  ownership and supports checked teardown. Host ASan/UBSan and AArch64 QEMU
  tests pass; QEMU installs the tables and maps/unmaps a distinct UART VA.
- Host tests pass with AddressSanitizer and UndefinedBehaviorSanitizer. The
  AArch64 QEMU guest runs FDT/resource, controller, installation and active
  MMIO tests; it explicitly reports that target GIC startup is not verified
  because no firmware FDT is available. The x86-64 and AArch64 builds compile
  the shared driver implementation. QEMU `virt` is not a Raspberry Pi 5
  emulator.

These are software milestones, not full G12 acceptance. Register models and
generic QEMU do not prove physical peripheral behavior. G12 remains
`IN_PROGRESS`; G13 remains blocked by its G12 dependency.

## Remaining software work: dependency-ordered plan

The historical software work below does not establish physical validation.

1. **Complete platform resource dependencies.** Guarded stage-1 installation,
   the bounded non-identity MMIO mapper and FDT-backed GIC startup wiring are
   implemented and software-tested. The MMIO mapper authorizes translated
   firmware `reg` and assigned PCI resources, uses Device attributes, and
   validates exact ownership and teardown. QEMU validates table installation
   and a UART map/unmap, but has no firmware FDT and therefore does not exercise
   target GIC startup. Keep IRQs masked until the OS has registered handlers;
   verify Group-1/CPU-interface state and device-to-interrupt routing on
   hardware when available. BCM2712 PERST release and failure reassertion now
   follow the pinned Linux sequence. Isolated start-link fixtures cover
   success/timeout, and the staged-startup register model now exercises both
   its MDIO-timeout rollback and a modeled successful startup/cleanup path.
   This is not hardware evidence; physical root-complex startup remains
   unverified. The pinned BCM2712 base nodes declare no PCIe clock or power
   domain, and Linux treats its `sw_pcie` clock as optional; the startup path
   rejects unexpected clock references before mapping. Do not invent a
   provider operation for optional-clock overlays. The read-only RP1 provider
   now derives rates for its registered, verified cases, including SYS, DMA,
   UART, ETH/ETH-TSU, PWM, SLOW_SYS, SDIO_TIMER, SDIO_ALT_SRC, ADC, GP0–GP5 and
   audio/camera clocks. Unsupported parent selections and unregistered USBH
   IDs remain fail-closed; rate queries do not activate peripherals or provide
   general clock enablement. Implement reset operations only for verified
   providers. The
   generic DMA page-pool allocator and cache-sync callback contract are
   implemented; GEM now exercises allocation and synchronization in fixtures,
   but no Pi controller runtime consumes the rings and platform cache operations
   are not verified. GIC register models and generic QEMU do not prove live Pi
   IRQ delivery. Fixed-clock support does not resolve firmware-controlled
   clocks.
2. **Complete PCIe/RP1 enumeration.** Preserve the pinned base-DTS reset-only
   assumption and reject unsupported optional clock resources. The modeled
   startup success and rollback paths pass, while physical startup remains a
   separate unverified requirement. Validate assigned buses and connect
   remaining RP1 function resources to the device ownership lifecycle. The
   bounded allocator, preassigned BAR reservations, transactional BAR
   assignment, in-range memory mapping and teardown are implemented and
   fixture-tested; RP1 BAR1 and MSI-X table/PBA mappings are owned, and MSI-X
   vectors are programmed and routed through MIP/GIC with rollback. GPIO,
   UART, SPI and I2C bind their discovered parent interrupts; remaining RP1
   function ownership and DMA use are incomplete. Inbound DMA windows are
   programmed transactionally from validated FDT ranges, but still need
   complete device ownership/use integration. MIP-to-GIC vector allocation
   and dispatch, PCI MSI-X capability discovery, endpoint table programming,
   APBS acknowledgement, generic handler lifecycle and a device-tree child
   IRQ binding API are implemented and fixture-tested. Integrate remaining
   validated RP1 resources and DMA consumers. The current staged startup
   requires the controller to report root-complex mode after reset and does
   not claim that RP1 is physically operational.
3. **Complete RP1 I/O integration.** Bind the tested UART, I2C and SPI
   register drivers to verified device-tree resources, clocks, interrupts and
   lifecycles. GPIO has host-scoped resource mapping and banked interrupt
   dispatch fixtures, but physical behavior is unverified. The
   pinned RP1 tree uses `arm,pl011-axi`, the `raspberrypi,rp1-clocks` provider
   and `skip-init`. The no-write `skip-init` PL011 adoption, validated
   resource-mapping lifecycle, and UART receive MSI-X/MIP/GIC callback
   integration are implemented and fixture-tested.
   SPI/I2C now have mapped child lifecycles and bind their parent interrupts
   through MSI-X/MIP/GIC, propagating controller errors to callbacks and active
   transfers. I2C SCL edge-time properties are parsed and used to calculate
   timing counts from the read-only `RP1_CLK_SYS` provider; no clock enable or
   power-management lifecycle exists, and disabled clock state is rejected.
   Other provider operations such as production clock enablement and power
   management remain incomplete. I2C and SPI retain their polling APIs and
   also provide bounded interrupt-driven transfers with caller-buffer
   ownership through completion or safe cancellation. Add applicable
   guest/emulator checks without treating synthetic fixtures as physical
   evidence.
4. **Implement microSD.** The software path now connects guarded 3.3-V SDHCI
   power to validated board regulator/GPIO ownership, performs bounded SDHC
   identification and CSD capacity decoding, and exposes single-block PIO
   reads/writes through `os_storage_device`. A separate GIC-v2-bound
   interrupt-driven one-block read/write path now has explicit completion and
   cancellation, bounded FIFO service, retained buffer ownership, error
   faulting and saved-register restoration. Host fixtures cover protocol,
   FIFO ordering, bounds, error/timeouts, data-line reset and restoration,
   plus interrupt completion/cancellation and error recovery. Physical
   pinmux, regulator, controller, interrupt delivery and card behavior remain
   unverified. Do not enable DMA or high-speed modes without verified support.
5. **Implement USB host/input/storage.** The pinned RP1 DWC3 nodes declare
   `dr_mode = "host"` and an edge-rising host interrupt. Resource attachment
   now rejects other modes and trigger configurations before mapping. The
   pinned `rp1-common.dtsi` nodes are disabled by default and declare no
   external clocks, resets, PHYs, power domains, VBUS supplies or
   `usb-psy-name`; attachment fails closed if any such dependency is declared.
   A successfully attached resource records this validated dependency-absence
   profile. Only in that profile may reset/clock/PHY/power prerequisite
   callbacks be omitted; supplied callbacks still have to pass, and
   interrupt-route and event-lifecycle callbacks remain mandatory. Unprofiled
   synthetic resources retain the strict six-callback policy. The generic
   Linux DWC3 core treats external reset/clock/PHY/power dependencies as
   optional and performs its own core/PHY/event setup before selecting host
   mode and creating xHCI. Do not infer firmware-owned clocks/PHY state.
   Inactive command/event/transfer ring
   resources, software-only enqueue/consume state, command-completion event
   decoding, bounded ERDP/IMAN acknowledgement, fail-closed startup prerequisite
   validation, bounded event dispatch and an MSI-X callback adapter are
   implemented. A modeled DWC3 child binding exercises the MSI-X/MIP/GIC
   callback route, event acknowledgement, bind rollback and unbind restoration.
   The DCBAA, scratchpad array and page buffers have bounded DMA ownership;
   halted-only DCBAAP/CRCR/ERST publication has modeled readback, rollback and
   retryable teardown. Enable Slot, per-slot contexts, Address Device and
   Disable Slot submission/completion are fixture-tested, including retryable
   DMA-safe DCBAA teardown. Non-control bulk/interrupt endpoint contexts for
   Low/Full/High Speed, Configure Endpoint submission/completion, Slot Context
   sizing, EP0/configured-endpoint doorbells and allocation/cache/MMIO rollback
   are now fixture-tested at `8c2da81`. Transfer rings have bounded
   single-Normal-TRB submission and Setup/Data/Status control TD construction
   with owned DMA-buffer synchronization and FIFO completion. BOT recovery,
   SCSI discovery and the `os_storage_device` adapter are fixture-tested.
   SuperSpeed bulk and interrupt endpoint contexts are fixture-tested for
   non-streaming endpoints. Bounded SuperSpeed isochronous descriptor parsing
   and inactive endpoint-context staging are fixture-tested, including
   companion Mult/payload bounds, endpoint type, interval, Max Burst, Max ESIT
   Payload and zero CErr; malformed descriptors fail closed. This does not
   implement isochronous TRB scheduling, streams or live transfers. USB
   enumeration, live HID input, SuperSpeed streams, production
   interrupt/event lifecycle integration and controller activation remain
   incomplete. External-dependency callbacks are not a substitute for
   core initialization. Keep host mode and IMAN interrupts disabled until
   production dependencies pass. Descriptor parsing and inactive fixtures do
   not establish
   live USB I/O or physical Pi behavior.
6. **Complete Ethernet and Wi-Fi.** GEM now has prerequisite-gated queue-base
   publication and engine start/stop, bounded TX start/reclaim and RX
   poll/rearm, plus a callback adapter for the shared network device API. A
   fixture now attaches the shared stack to that adapter and validates DHCP
   Discover transmission, OFFER processing through the GEM RX ring, REQUEST
   transmission through GEM TX, and final DHCP ACK lease configuration. The
   controller is not started. A fixture-tested owner-bound ETH/ETH-TSU
   gate lease enables only the registered clocks, preserves their configured
   rates, borrows already-enabled gates, and restores owned gates only after
   the bound controller is idle. No production GEM startup caller uses this
   lease yet. Reset/PHY, power, DMA/cache, interrupt and controller readiness
   integration, Wi-Fi hardware support and physical operation remain. Verify
   each dependency from pinned platform evidence; test bounds, failure rollback
   and teardown. Continue with HDMI/audio/camera requirements while preserving
   the explicit Ethernet readiness limitation. Generic VM NIC tests
   remain shared-stack evidence only, not Pi-driver evidence. The pinned Pi 5
   board tree places its `brcm,bcm4329-fmac` WLAN function on SDIO2 with a
   board supply and 4-bit bus; bounded CMD52 plus metadata, mapping and
   callback-only supply-policy fixtures are software layers only. Bounded
   CMD5 query and operating-condition negotiation now decode the SDIO R4
   function count/OCR and poll ready using only the host/card voltage
   intersection, while rejecting changing card capabilities. These are
   protocol helpers only. Production
   clock/reset/power/pinmux/DMA/interrupt verifiers and an owner-bound
   production WLAN caller remain outstanding; these helpers do not write CCCR
   or activate SDIO2.
   The owner-scoped RP1 GPIO output lease API and callback adapter are
   implemented, but do not activate SDIO2. CCCR function enable/readiness and
   CMD53 byte/block PIO are fixture-tested. Chip-specific firmware lifecycle,
   production DMA/interrupt readiness providers, an owner-bound production
   WLAN caller, and network-stack binding remain outstanding.
7. **Implement HDMI, audio and camera interfaces.** HDMI FDT/MMIO ownership,
   bounded DDC base/first-extension reads, EDID base parsing, CTA revision-3
   parsing and filtering of selected progressive VICs 1-4, 16-19 and 31-34,
   fail-closed validation of pinned clock/reset/interrupt bindings,
   firmware-managed HDMI/BVB clock query/rate leases, owner-checked DVP
   quiescing, and HVS core/display firmware-clock prerequisites (IDs 4 and 16)
   are fixture-tested. A bounded owner now maps the pinned HVS MMIO window and
   validates its zero-cell IOMMU4 reference and `disp_intr`/GIC binding with
   rollback and teardown coverage. The pinned base device tree disables HVS,
   both HDMI ports and `disp_intr`, and declares no HDMI/HVS pinctrl state.
   The owner-bound HDMI child IRQ dispatcher remains masked until display
   prerequisites pass. Live HVS IRQ binding, IOMMU hardware programming,
   display mode setup and output remain disabled; do not connect a
   display-register caller until those dependencies are verified.
   I2S/DMA target validation and transactional channel ownership are implemented;
   the pinned base tree has a disabled audio-out node and no I2S nodes. Audio
   configuration/streaming and camera operation still require verified
   controller, clock, pinmux, cache and interrupt dependencies.
8. **Complete G12 acceptance.** Build both architectures; run all driver
   fixtures, static checks, applicable emulation and required regressions.
   Update capability states only for completed and software-tested interfaces.
   Physical hardware is not a prerequisite for software acceptance.

## Software work versus physical verification

PCIe/RP1 BAR programming and driver binding, complete binding/resource integration and the USB,
microSD, HDMI, Ethernet, Wi-Fi, audio and camera interfaces remain software
requirements. GPIO has tested host-scoped FDT resource mapping and parent
interrupt dispatch; PL011 has host-scoped resource mapping and synthetic
receive-interrupt dispatch through MSI-X/MIP/GIC, while I2C and SPI have tested
resource mapping and synthetic interrupt lifecycles. RP1 DWC3 has resource
mapping, inactive DMA ring fixtures, a modeled MSI-X/MIP/GIC event route and
halted-only register publication with rollback fixtures. Production
interrupt/event lifecycle integration and controller startup/host operation
remain incomplete. The attached DWC3 profile only permits omission of external
reset/clock/PHY/power callbacks after their absence is validated; it does not
perform core initialization or enable the disabled controller.
The clocked I2C/SPI attachment paths obtain `RP1_CLK_SYS`; GEM-related
`RP1_CLK_ETH`/`RP1_CLK_ETH_TSU`, the `RP1_CLK_SLOW_SYS`/SDIO/ADC/GP clock
cases, and audio/camera-related `RP1_CLK_DMA`/`RP1_CLK_AUDIO_OUT`/
`RP1_CLK_I2S`/`RP1_CLK_MIPI0_CFG`/`RP1_CLK_MIPI1_CFG` rates are read-only and
fixture-tested. RP1 audio-out and
I2S0/I2S1 DMA resource metadata, MMIO ownership, and transactional TX/RX
channel reservation tied to the discovered DMA provider are fixture-tested,
and the Audio Out register lifecycle is fixture-tested behind explicit clock,
DMA-cache and interrupt verifiers. No production provider/caller is connected,
the AXI-DMA controller is not activated and no audio stream is established.
GEM resource attachment, inactive DMA rings, packet-state transitions, a
modeled interrupt lifecycle, and owner-bound ETH/ETH-TSU clock-gate leasing
are fixture-tested. The clock lease is not wired to a production startup
caller. Reset/PHY, power, DMA/cache, interrupt and controller readiness remain
unintegrated or unavailable, so live Ethernet/network operation remains
incomplete.
USBH clock IDs and clock enablement
remain unsupported. No physical peripheral operation or interrupt delivery
is verified.
BCM2712 SDHCI has validated resource mapping, bounded reset-all polling, a
validated AON GPIO supply/card-detect callback, a 400 kHz initialization-clock
path, bounded SDHCI command/response polling, SDHC identification and capacity
decoding, plus modeled single-block PIO reads/writes attached through
`os_storage_device`. A distinct GIC-v2-bound interrupt-driven one-block
read/write API is fixture-tested for bounded FIFO service, completion,
cancellation, error recovery, buffer ownership and register restoration.
DMA and high-speed modes remain unsupported; physical storage and interrupt
delivery remain unverified.

`HARDWARE_NOT_VERIFIED` remains appropriate for board firmware handoff, actual
controller/device enumeration, electrical behavior, real storage/display/
network/peripheral operation and board-specific stability. Keep those claims
unverified even after software acceptance. Lack of a physical board does not
prevent software acceptance when the approved static, fixture and applicable
emulation criteria pass.

## Limits

The AArch64 QEMU test uses synthetic FDT and PCI-register data; it executes the
BCM2712 reset, MDIO/post-setup, config-access, RP1 BAR and MSI-X lifecycle
fixtures but does not model
BCM2712 PHY, reset, clock, power or link-training behavior. The pinned BCM2712
base PCIe nodes declare reset controls but no clocks or power domains; the
Linux driver's `sw_pcie` clock is optional. This implementation rejects an
unexpected clock reference and does not support optional-clock overlays. The
startup fixture retains an MDIO-timeout rollback case and uses a separate
register-model callback to emulate MDIO completion for the software success
path; neither case models physical hardware. Any UEFI-provided guest FDT is
reported but is not treated as Pi hardware evidence.
The FDT resource layer decodes GIC-400 interrupts, bounded RP1 two-cell IRQs
and fixed-clock frequencies. A GICv2 SPI registration/dispatch path,
FDT-authorized mapper-backed startup and an AArch64 IRQ vector entry are
implemented and fixture-tested. QEMU lacks an FDT, so target startup and live
interrupt routing remain unverified. Other firmware-controlled clock rates
and reset providers remain unresolved. DMA allocation uses the kernel physical page
pool with explicit cache-sync callbacks; the xHCI ring fixtures exercise that
allocator, but no active Pi driver consumes DMA buffers. No physical Raspberry
Pi 5 peripheral behavior is claimed.

The SDIO2 clock-state verifier is based on the pinned
[`bcm2712-ds.dtsi`](https://github.com/raspberrypi/linux/blob/31a34885388835dce2460744d7961f2b9e7dc416/arch/arm64/boot/dts/broadcom/bcm2712-ds.dtsi),
[`sdhci-brcmstb.c`](https://github.com/raspberrypi/linux/blob/31a34885388835dce2460744d7961f2b9e7dc416/drivers/mmc/host/sdhci-brcmstb.c)
and [`sdhci.h`](https://github.com/raspberrypi/linux/blob/31a34885388835dce2460744d7961f2b9e7dc416/drivers/mmc/host/sdhci.h).
The reset-state verifier follows the pinned
[`sdhci.c`](https://github.com/raspberrypi/linux/blob/31a34885388835dce2460744d7961f2b9e7dc416/drivers/mmc/host/sdhci.c)
semantics that hardware clears the reset mask after completion:
the board tree supplies a fixed 200-MHz input clock, and the verifier checks
only SDHCI status bits.

References: [Flattened Devicetree specification](https://devicetree-specification.readthedocs.io/en/latest/chapter5-flattened-format.html),
[Raspberry Pi I/O controller documentation](https://www.raspberrypi.com/documentation/computers/io-controllers.html),
[pinned BCM2712 device tree](https://github.com/raspberrypi/linux/blob/31a34885388835dce2460744d7961f2b9e7dc416/arch/arm64/boot/dts/broadcom/bcm2712.dtsi),
[pinned Raspberry Pi 5 board clock bindings](https://github.com/raspberrypi/linux/blob/31a34885388835dce2460744d7961f2b9e7dc416/arch/arm64/boot/dts/broadcom/bcm2712-rpi-5-b.dts),
[pinned BCM2712 display-system device tree](https://github.com/raspberrypi/linux/blob/31a34885388835dce2460744d7961f2b9e7dc416/arch/arm64/boot/dts/broadcom/bcm2712-ds.dtsi),
[pinned BCM2711 DVP clock/reset driver](https://github.com/raspberrypi/linux/blob/31a34885388835dce2460744d7961f2b9e7dc416/drivers/clk/bcm/clk-bcm2711-dvp.c),
[pinned Raspberry Pi firmware clock IDs](https://github.com/raspberrypi/linux/blob/31a34885388835dce2460744d7961f2b9e7dc416/include/soc/bcm2835/raspberrypi-firmware.h),
[pinned Broadcom PCIe host driver](https://github.com/raspberrypi/linux/blob/31a34885388835dce2460744d7961f2b9e7dc416/drivers/pci/controller/pcie-brcmstb.c),
[pinned Raspberry Pi `rp1.dtsi`](https://github.com/raspberrypi/linux/blob/31a34885388835dce2460744d7961f2b9e7dc416/arch/arm64/boot/dts/broadcom/rp1.dtsi),
[pinned Raspberry Pi `clk-rp1.c`](https://github.com/raspberrypi/linux/blob/31a34885388835dce2460744d7961f2b9e7dc416/drivers/clk/clk-rp1.c),
[pinned Raspberry Pi DRM EDID/CTA mode table](https://github.com/raspberrypi/linux/blob/31a34885388835dce2460744d7961f2b9e7dc416/drivers/gpu/drm/drm_edid.c),
[pinned DesignWare I2C timing implementation](https://github.com/raspberrypi/linux/blob/31a34885388835dce2460744d7961f2b9e7dc416/drivers/i2c/busses/i2c-designware-master.c),
[pinned DesignWare SPI register definitions](https://github.com/raspberrypi/linux/blob/31a34885388835dce2460744d7961f2b9e7dc416/drivers/spi/spi-dw.h),
[pinned DWC3 core initialization](https://github.com/raspberrypi/linux/blob/31a34885388835dce2460744d7961f2b9e7dc416/drivers/usb/dwc3/core.c),
[pinned xHCI ring definitions](https://github.com/raspberrypi/linux/blob/31a34885388835dce2460744d7961f2b9e7dc416/drivers/usb/host/xhci.h),
[Linux RP1 PCI driver](https://github.com/torvalds/linux/blob/master/drivers/misc/rp1/rp1_pci.c),
[Linux PCI IDs](https://github.com/torvalds/linux/blob/master/include/linux/pci_ids.h).

## Increment 4 - Ethernet/GEM startup composition

- Added kernel/pi5_gem_host.{c,h}: capabilities read, DMA rings, MSI-X/MIP IRQ bind, publication, prerequisite-gated controller start and network device binding, with full rollback and re-bringup. Shutdown halts TX/RX, drops unsent frames, restores publication, unbinds IRQ and releases rings.
- Tests: host plain and ASan/UBSan composition test (missing power check, MMIO write failure at TBQP0/RBQP0/TBQPH/RBQPH/IER0/NCR, DMA allocation failure, two bringup/shutdown rounds). G12 gate SOFTWARE_PASS including the AArch64 QEMU guest.
- Remaining blocker: no production power-feedback check, so the physical GEM path stays fail-closed. Fixture/register-model only; no live-link claim.

