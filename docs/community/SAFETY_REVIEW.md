# Targeted v1 safety corrections and evidence

Date: 2026-11-14. Corrective base:
`b9ce8e49252fbd309987df501437f0b58ced996e`. This closes the three
previously recorded software findings, not G00-G22, a new full audit or
physical certification. Earlier packages/evidence remain historical.

## Reproduction and corrections

| Original finding | Observed root cause and reproduction | Correction and regression result |
|---|---|---|
| C17-MAP: descriptor bounds/alignment/arithmetic | The probe summarized firmware descriptors before invoking the existing MMU validator. Mechanical extraction into pi5_probe_support.c preserved the defect; the new test failed because a misaligned 41-byte stride produced a summary before rejection. | The shared production validator now runs before any descriptor summary read or arithmetic. Rejected layouts return zero summary and the probe emits a FAIL marker without publishing statistics. PASS: invalid stride, truncation, buffer alignment, zero size, descriptor version, page multiplication and physical-end overflow. |
| C17-UART: lost diagnostic text | ConOut incremented the only text pointer, leaving UART an empty suffix. The multi-chunk fan-out assertion failed before correction. | An independent console cursor preserves the original pointer for UART. PASS: complete 290-byte output through both sinks, missing console, missing UART and console failure. This does not detect physical UART delivery or hardware timeout. |
| OWNERSHIP-CONCURRENCY: overlapping live allocations | Ordinary scan/store ownership allowed concurrent single/contiguous allocators to return overlapping ranges. Eight host threads reproduced overlapping live allocation addresses before correction. | A per-pool acquire/release try-lock covers scan, zeroing, publication, owner validation and free. Contended calls fail without mutation rather than spin in a preempting interrupt. Atomic owner accesses also cover free-count and the two DMA/cache observers. PASS: 24,000 mixed requests per execution, disjoint live bytes, physical alignment, wrong owner, double free and deterministic busy-lock retention. |
| Directly related pool bounds | Contiguous allocation omitted the maximum pool-size guard. The new test reproduced successful allocation with page_count=65. | Shared pool validation rejects that metadata; initialization also rejects virtual span overflow. PASS: invalid-count rejection and zeroed failure output. |

The descriptor defect above concerns the UEFI memory map, not a newly
demonstrated DMA linked-list descriptor defect. Existing affected Pi driver
fixtures exercise DMA descriptor/range/size/alignment and release behavior;
they were rerun without redesigning hardware drivers.

## Commands and actual local evidence

Executed under WSL/Linux:

- `sh scripts/test-community-safety-host.sh` -> PASS with strict C17
  `-Wall -Wextra -Werror`, plain, ASan/UBSan with non-recovering diagnostics,
  and ThreadSanitizer. All four test groups execute in each variant.
- `sh scripts/test-memory-host.sh` -> PASS, plain and ASan/UBSan.
- `sh scripts/test-pi5-host.sh` -> PASS, existing DMA/device-tree/MMU/runtime/
  DWC3 host models, plain and ASan/UBSan. Not board or privileged ARM64 execution.
- `EXPERIMENTALOS_PROBE_DIR=build/community-checks/probe-dev2-local sh scripts/build-pi5-probe.sh`
  -> PASS with LLVM/LLD 21.1.8 and strict AArch64 C17 diagnostics; compilation only.

`python3 scripts/release/community_safety.py` -> PASS with Clang 21.1.8.
Its corrective scope is the
complete memory, probe-support and probe-entry translation units plus five
DMA allocation/ownership/cache entry functions in pi5_driver.c. Commands,
diagnostics and exit statuses are retained in
`build/community-checks/static-analysis.json` and the existing private CI
evidence artifact. Clang requires one named function per invocation: a
comma-separated filter was observed to skip every function despite exit 0.
The runner now invokes each DMA function separately, requires actual
path-analysis progress, and has a focused regression rejecting that false
success. No warning or empty analysis is silently converted to PASS.

An initially expanded, whole-driver analyzer run emitted
`core.uninitialized.Assign` in `usb_copy_bytes`, along the
`os_pi5_xhci_hid_poll_report` path (working-tree line 10186 before corrections).
That USB path is outside the recorded allocator/probe corrective scope and
was not changed or declared resolved. The expanded result is retained locally
as `build/community-checks/static-analysis-expanded-preexisting.json`.
Physical USB remains blocked; the scoped analysis must not be described as
a clean whole-driver analysis.

## Ownership and lifetime contract

Initialize a pool and its backing storage before publishing it; do not
reinitialize or destroy them while users exist. All mutators return NULL/0
on contention and retain ownership on failed release. Retry/defer a busy
release from a suitable non-interrupt context; do not wait in a preempting
interrupt for the interrupted owner. Concurrent free-count is advisory.
Existing single-thread API results are preserved.

An allocation owner must stop all CPU/device use, finish DMA and required
cache synchronization before freeing. Atomic ownership observation is not
a lifetime pin or permission to free concurrently with a live device.
The host race tests establish allocator serialization, not IRQ delivery,
device quiescence, real DMA ordering or physical cache coherency.

## Physical gate remains separate

| Requirement | Result and consequence |
|---|---|
| Recorded map, UART fan-out and allocator software defects | PASS at focused software-regression scope only. |
| Target-specific ordinary Pi entry and firmware handoff | BLOCKED. The ordinary AArch64 image remains a QEMU/development artifact; the probe is separate and not physically approved. |
| MMIO mapping, register widths, volatile access and barriers on BCM2712/RP1 | NOT_VERIFIED on equipment. No new MMIO initialization was introduced. |
| Physical IRQ/DMA ownership, device lifetime and cache coherency | NOT_VERIFIED. Peripheral/traffic tests remain BLOCKED. |
| Electrical supply, polarity, pinout, voltages, currents, backfeeding and shorts | NOT_VERIFIED. Complete ELECTRICAL_PRE_POWER.md and obtain independent engineering approval of a board-specific physical procedure. |
| Firmware/DTB compatibility and redistribution, media writes | BLOCKED as before. |
| Offline package boundary | PASS through existing focused package tests; serial access remains refused. |

SAFETY_GATE.json still has no approved physical operations. No clean compiler,
sanitizer, hosted ARM64 or QEMU result can fill a missing physical measurement.
