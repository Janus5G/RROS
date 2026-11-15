# Community hardware safety guide

[Dansk](COMMUNITY_SAFETY.da.md) | [Existing hardware guide](HARDWARE_GUIDE.md) |
[Safety gate](SAFETY_GATE.json) | [Evidence template](EVIDENCE.md)

This guide explains the existing gate; it does not create new test gates or
approve a procedure. The current gate approves **no physical operations**.
The downloaded `1.0.0-dev.2` assets and PDF are fixed historical snapshots;
this document provides additional guidance, not an update to those assets.
No statement here establishes that a configuration is safe.

## Result vocabulary

Record a result for each existing requirement and the exact configuration:

- **PASS**: the stated acceptance criteria for that particular check were met
  and supported by inspectable evidence. A review or software PASS is limited
  to its stated scope; it is not permission to proceed or hardware PASS.
- **FAIL**: a criterion was not met, a contradiction was found, or a test
  produced an unacceptable result. Stop and retain the evidence.
- **BLOCKED**: an operation cannot proceed because a prerequisite, approved
  procedure or authorization is missing or a relevant critical gate is
  FAIL, BLOCKED or NOT_VERIFIED.
- **NOT_VERIFIED**: the necessary observation, measurement, review or evidence
  has not been completed or accepted. Do not infer success.

The project-wide statuses distinguish SOFTWARE_PASS from
HARDWARE_NOT_VERIFIED/HARDWARE_PASS. A hosted, simulated, build or source
result cannot establish HARDWARE_PASS. A submitted measurement is evidence
for review, not automatic safety approval or a gate update.

## Level A: disconnected review

Qualified contributors may review the following with all components
disconnected and unpowered. Record the evidence in the
[template](EVIDENCE.md); the detailed existing criteria are E01-E08 in the
[electrical checklist](ELECTRICAL_PRE_POWER.md).

| Review | Evidence and acceptance | Stop if |
|---|---|---|
| BOM and compatibility | Compare [BOM.csv](BOM.csv), quantities, exact board revision, memory/stepping, accessories, supply and cable against applicable manufacturer documentation. Record document revisions and discrepancies. PASS only for the items actually confirmed. | A part, revision, rating or compatibility is unknown or contradictory. |
| Datasheets and voltage | Record both endpoint limits, supply profile, GPIO/UART logic levels, pin current and aggregate/port limits with source citations. Record an itemized current budget. PASS only when all required ratings and margins are documented and compatible. | A limit, load, polarity or margin is missing, incompatible or guessed. Datasheet review does not replace measurement. |
| Wiring, polarity and connectors | Review a proposed, unconnected pin-to-pin table against the exact board/connector drawings: pin 1, viewing direction, signals, reference, power conductors and backfeeding risks. Independent qualified second review is required. | Orientation, signal, polarity or review is uncertain; do not attach UART, GPIO or power. |
| Assembly and cooling | Inspect disconnected components, mounting clearances, exposed conductors, debris and short paths; compare cooler/enclosure instructions. Record redacted photographs and discrepancies. | Damage, metal contact, insufficient clearance or uncertainty. Do not infer a correct assembly from a picture. |
| Unpowered continuity/short review | A qualified engineer must first define and independently review the unpowered inspection procedure, test points, instrument suitability and acceptance limits (E05/E07). Only then may a qualified contributor perform the separately authorized disconnected check; record instrument, conditions, readings and interpretation. | Unknown stored energy, exposed supply or other source, uncertain test points, missing procedure/limits, or an unexpected result. Do not use continuity mode on an energized circuit; a beep alone proves neither safe power rails nor a safe configuration. |
| Source and hosted software | Use the existing [software commands](RELEASE.md#supported-software-checks) and [review evidence](SAFETY_REVIEW.md). Record commit, tool versions, exact commands and outputs. A PASS applies only to the actual host or simulated environment. | A required test fails, cannot run or lacks provenance; do not relabel missing results as PASS. |

Unpowered inspection evidence does not authorize connections, live
measurements, power-up, media writes or a physical RROS test. No numerical
pinout, current limit or acceptance threshold is invented by this guide.

## Level B: conditional electrical verification

No energized measurement procedure is currently approved by this guide or
the [gate](SAFETY_GATE.json). Before a qualified contributor performs any
separately proposed electrical measurement, all of the following must be
documented for the **exact configuration and measurement scope**:

1. The applicable E01-E08 evidence is complete, discrepancies resolved,
   components identified and a qualified second reviewer has checked pinout,
   polarity, voltage compatibility, current budget, wiring and backfeed risks.
2. A qualified electrical engineer has defined a bounded, board-specific
   procedure identifying the equipment, rated instrument and range,
   measurement points, isolation, limits, uncertainty, stop conditions and
   recovery. The reviewer must independently approve that procedure **before**
   execution; applicable site, equipment and owner authorization is required.
3. The procedure explicitly identifies what, if anything, may be energized,
   its power boundaries and the relevant prerequisite gate disposition.
   The existing `power-up` and connection blocks are not overridden here:
   an assembled-system power-up or UART/GPIO connection needs a separately
   approved procedure and updated relevant gate disposition.
4. Record the configuration, date, procedure revision, equipment/instrument
   ID and range, conditions, points, values and units, uncertainty, applicable
   acceptance limits, unexpected observations and redacted photographs.
   Review results against the limits; do not change limits after seeing data.
5. A qualified engineer **independent of the measurement performer** reviews
   the results and unresolved risks. The project safety-gate reviewer records
   any applicable gate disposition and explicitly approves the next bounded
   operation. A contributor's result alone does neither.

Stop for missing approval, ambiguous wiring/limits, unexpected readings, heat,
smell, damage, reset, overcurrent or any failed critical requirement. The
checklist provides no live wiring or power-up instructions. Completing Level B
does not automatically authorize powering the assembled system or running RROS.

## Level C: operations currently blocked

The table gives **necessary evidence**, not permission to perform the blocked
test to obtain it. Any proposed evidence-gathering operation itself needs its
own prerequisites, independently reviewed procedure and applicable
authorization. A qualified engineering reviewer and project safety-gate
reviewer must assess evidence and explicitly approve each next operation.
Hardware-dependent evidence may remain NOT_VERIFIED until such a separate
operation is approved.

| Blocked operation | Unresolved risk / existing requirements | Evidence needed before considering that operation |
|---|---|---|
| Native RROS or probe boot | Generic AArch64 entry is QEMU-oriented; physical entry, handoff, firmware/media and electrical configuration are unverified: `TARGET-ENTRY`, `FIRMWARE-MEDIA`, `ELECTRICAL`. | Reviewed Pi-specific entry and handoff, exact board/firmware/media compatibility and hashes, completed electrical review, and a separately approved boot/media procedure. |
| GPIO/MMIO access | BCM2712/RP1 mappings, register validity, access widths, barriers and electrical pin configuration are unverified: `MMIO`, `ELECTRICAL`. | Independently reviewed board-specific register map, mapping/width/ordering and pin configuration; completed electrical evidence; a scoped access procedure and appropriate prior hardware evidence. |
| UART connection | Connector orientation, both endpoint voltages, reference, polarity, backfeeding and delivery are unverified: `ELECTRICAL`, `MMIO`. Software fan-out PASS does not verify physical UART. | Independently reviewed exact connector/pin table and adapter specifications, measured compatible levels under an approved procedure, backfeed/current review and a separately approved connection/capture procedure. |
| DMA/cache-coherency testing | Physical DMA address translation, alignment, ownership/lifetime, synchronization, cache maintenance and ordering remain unverified: `IRQ-DMA`, `MMIO`. Allocator serialization alone does not establish device quiescence. | Reviewed platform DMA mapping and cache/ordering design, controlled ownership and quiescence evidence, required target/register and electrical prerequisites, and a separately approved bounded DMA procedure. |
| Interrupts and peripherals, including PCIe/RP1, USB, Ethernet, storage, display, camera, audio and wireless | Register/interrupt configuration, firmware, power, device behavior and, where applicable, DMA are unverified: `MMIO`, `IRQ-DMA`, `ELECTRICAL`, `FIRMWARE-MEDIA`. | Board- and device-specific register/interrupt/resource review, compatible firmware and power evidence, required DMA/cache evidence, and separate approval of each peripheral procedure. USB also retains the unrelated analyzer warning below. |
| Firmware update, media write, power sequencing or other configuration-dependent operation | Board/firmware/media compatibility, redistribution rights, destructive effects, power and exact configuration are unresolved: `FIRMWARE-MEDIA`, `ELECTRICAL`, `TARGET-ENTRY` where applicable. | Exact board/firmware compatibility and artifact hashes; rights clearance as applicable; backups and explicit authorization for destructive operations; reviewed power sequencing and separately approved operation-specific procedure. |
| Robot actuation | Unverified hardware, power, outputs and external loads; no actuator safety review or procedure. | Independent system-level electrical and mechanical risk review, isolation and emergency-stop design, all prerequisite device gates, and explicit operation-specific authorization. |

The existing SW-01 to SW-05 and HW-01 to HW-03 descriptions remain in
[HARDWARE_GUIDE.md](HARDWARE_GUIDE.md#6-test-gates-and-expected-results).
Earlier private dev.2 records document passing community package CI, native
ARM64 software CI and CR-01–CR-07 at commit
`df8e20e76580898c57a25a98049aa72419b9a86e`; the historical run records are
not included with this distribution, and none is physical Pi evidence. No CI
has been run for this new distribution. An
expanded whole-driver analysis also reported
`core.uninitialized.Assign` in `usb_copy_bytes` along
`os_pi5_xhci_hid_poll_report`; it is unrelated to the corrected probe/page
findings and remains unresolved. The scoped analyzer PASS is not a clean
whole-driver result. See [SAFETY_REVIEW.md](SAFETY_REVIEW.md).
