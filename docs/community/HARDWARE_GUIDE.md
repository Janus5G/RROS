# RROS v1 / Raspberry Pi 5 engineering review

## 1. Read the gate before handling hardware

This community guide is intentionally an **engineering-review document**.
It is not a guarantee of safety and does not approve physical boot, wiring,
power-up, formatting or peripheral operation. Software PASS is not hardware
PASS. Failed, blocked or unverified safety-critical requirements in
SAFETY_GATE.json keep the corresponding operation blocked.

For the current three-level community guidance see
[English](COMMUNITY_SAFETY.md) or [Dansk](COMMUNITY_SAFETY.da.md).
These documents supplement the downloaded historical dev.2 PDF; they do not
change its release assets or approve physical operations.

The owner does not have to perform hardware validation before volunteers
join the project. Volunteers can inspect this document, source and existing
logs now; a qualified engineer must first resolve and document each relevant
gate before a separate, board-specific physical procedure is approved.
No robot, motor, PLC, actuator, battery pack or external power stage belongs
in the initial review configuration.

## 2. Shopping list and compatibility

Use BOM.csv for quantities and specifications. The primary board target is
Pi 5 Model B, 4 or 8 GB. Record exact PCB revision, RAM, BCM2712 stepping,
cooling, PSU/cable and every attached accessory. Other revisions are not
silently treated as compatible.

The official 27 W USB-C supply advertises a 5.1 V / 5 A profile. That is a
supply capability, not a measured RROS current budget or approval to feed
power through GPIO. Never infer connector polarity or wiring from wire
colour, a photograph, this concept illustration or a different Pi model.
GPIO/UART logic must be verified against current vendor documentation;
3.3 V logic is not 5 V tolerant. No adapter supply wire should be used as a
substitute for the board's reviewed USB-C supply.

Vendor sources for independent checking:
https://www.raspberrypi.com/products/27w-power-supply/
https://www.raspberrypi.com/documentation/computers/raspberry-pi.html
https://www.raspberrypi.com/documentation/microcontrollers/debug-probe.html

Vendor pages could not all be retrieved during preparation. Therefore
connector orientation, exact pin assignments, polarity, available current,
continuity and voltage are NOT_VERIFIED, not approved by citation alone.

Complete ELECTRICAL_PRE_POWER.md before proposing any physical procedure.
Its E01-E10 checklist requires supply/profile and cable review, GPIO/UART
voltage limits, independently checked pinout/polarity, wiring/short inspection,
per-pin and aggregate current budgets, measurement evidence and second review.
Every result requiring actual equipment remains NOT_VERIFIED. The checklist
authorizes neither wiring nor live measurements/power-up.

## 3. Numbered assembly-review sequence

1. Keep all components disconnected and unpowered. Inventory components
   against BOM.csv and photograph labels with serial numbers redacted.
2. Review vendor cooler and enclosure instructions. Inspect possible
   fastener, heatsink, conductive-surface and cable short-circuit paths.
   Do not assume a cooling assembly is fitted correctly from an illustration.
3. Identify the board revision and exact debug connector in vendor drawings.
   Write down a reviewed pin-number/orientation/signal table independently.
   This guide deliberately supplies no unverified physical pin numbering.
4. Have a qualified second reviewer check 3.3 V logic, common-reference
   requirements, direction of UART signals and power/backfeeding risks.
   Do not attach UART, GPIO, USB, PCIe, camera or power based on this guide.
5. Record the missing electrical measurements and component current budget.
   Do not perform live measurements, power-up or hot-plugging as a step here.
6. Submit the review and proposed incremental procedure. Disconnected
   continuity/short checks require the separate, independently reviewed
   E05/E07 inspection procedure. Until relevant gate approval is recorded,
   stop at disconnected inspection and offline software tests.

The PDF's disconnected-block illustration is an inspection map, not a
pinout, wiring schematic, photograph or evidence of a working Pi.

## 4. Firmware and installation prerequisites

The existing firmware research pin is Pi UEFI v0.3. See
scripts/pi5_hw/boot_chain_pins.json for archive/per-file hashes and the license
inventory for TF-A, EDK2, non-OSI modules, Broadcom and DTB uncertainties.
Firmware is not included. Firmware source tags do not establish complete
binary composition or redistribution rights.

C1/D0 stepping, display, ACPI/Device Tree and firmware restrictions require
independent review against the precise board/firmware combination. Do not
blindly update EEPROM or treat a firmware setup screen as a native RROS
display test. Media writes destroy data; this review kit contains no
community-approved flashing, formatting or EEPROM procedure.

The ordinary AArch64 EFI software build is a generic development artifact
with QEMU-specific startup, NOT an approved physical Pi image. The diagnostic
probe is separate. Its recorded map, UART fan-out and allocator software
findings now have focused regression fixes, but physical entry, MMIO,
firmware and electrical requirements remain unverified. Neither artifact is
approved for physical execution. Do not copy either to a Pi card under this guide.

## 5. Software-only rehearsal

The review ZIP supplies community_tester.py and the existing parser. Python
3.11+ is required. No serial dependency, serial enumeration, port opening,
device writes, email transmission or administrator privilege is needed.

    python community_tester.py demo --out rehearsal

The clearly synthetic UART text exercises parsing, raw-log preservation,
JSON results, checksums and report.zip. It is not physical evidence.
Import an existing, legitimately obtained log with collect --log and the
explicit artifact/revision/hash arguments shown by --help. --port is refused
before the old serial collector can be invoked. A pre-existing output
directory is refused so previous evidence is not overwritten.

    python community_tester.py --help
    python community_tester.py collect --help

## 6. Test gates and expected results

SW-01: clean source checksum verification and deterministic ZIPs -> PASS
only when every digest and repeated output matches.
SW-02: fresh x86-64/AArch64 EFI build -> PASS for compilation/linking and
byte reproducibility only; NOT_VERIFIED for physical Pi execution.
SW-03: CR-01 through CR-07 -> actual independent GitHub job conclusions.
SW-04: ARM64 hosted memory/process/storage/network -> software results only;
privileged G12 cache instructions are not an approved hosted EL0 test.
HW-01: electrical, pinout, power/current/short-circuit review ->
NOT_VERIFIED; physical connection and power-up BLOCKED.
SW-05: malformed-map rejection, full UART fan-out and serialized allocation ->
PASS only with the focused strict C17, ASan/UBSan and TSan regressions.
HW-02: physical probe boot/firmware entry and UART delivery -> NOT_VERIFIED;
physical probe boot and connection remain BLOCKED despite SW-05.
HW-03: MMIO, physical IRQ/DMA ownership and cache coherency -> NOT_VERIFIED;
native peripheral and DMA tests remain BLOCKED. Allocator serialization does
not prove device quiescence or authorize freeing an in-flight DMA buffer.

There are no approved native USB, Ethernet, display, camera, audio, wireless,
storage or robotics operations in this delivery. Discovery metadata does
not demonstrate device operation. QEMU is not a Pi 5 hardware emulator.

## 7. Evidence and stop conditions

Each result needs PASS, FAIL, BLOCKED or NOT_VERIFIED, a reason, execution
environment, source commit, artifact SHA-256 and exact commands. Preserve raw
logs locally. State log redactions and distinguish vendor OS, firmware,
probe, QEMU, native host and actual native RROS observations.

Record component quantities, revisions, cable identities, firmware hashes,
proposed measurement points, instrument/range, conditions, elapsed time,
failures and recovery decisions. Photographs should show only relevant
hardware, with no people, location, account details or serial identifiers
unless genuinely needed and voluntarily approved.

Any unexpected heat, smell, damaged connector, uncertain pin/polarity,
backfeeding, undervoltage, uncontrolled reset, contradictory documentation,
unreviewed firmware, unknown current budget or failed gate is a stop
condition. No live troubleshooting procedure is supplied here. Hardware
observations cannot be upgraded to PASS automatically by a log parser.

## 8. Collaboration and limitations

Use Discussions for engineering review, Issues for reproducible defects and
Pull Requests for proposed changes. Review and redact archives before
voluntary submission. Never post credentials, tokens, private keys, Wi-Fi
details, MAC/IP addresses, home paths or personal contact information.
Email is not required. Do not submit a full disk image or firmware blob.

The PDF, BOM and offline tools are GPL-covered original project material.
Third-party firmware, toolchains and captured upstream fixtures retain
separate terms. Public repository/release publication is still an owner
decision. Review all current findings in SAFETY_GATE.json; this guide
does not override them or certify any physical configuration.
