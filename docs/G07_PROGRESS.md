# G07 network foundations progress

- Gate status: `SOFTWARE_PASS`
- Hardware status: `HARDWARE_NOT_VERIFIED`

## Safety boundary

G07 tests use a deterministic in-memory VM network link with two bounded
endpoints. The test driver does not connect to a host interface, LAN, Internet,
or physical device.

## Implemented and tested so far

- Bounded Ethernet/ARP/IPv4, ICMP, UDP, DHCP, DNS and TCP protocol handling.
- Deterministic two-endpoint VM NIC; no host interface or external network use.
- Host tests exercise DHCP/DNS state, UDP payloads, a TCP handshake, ARP,
  ICMP, malformed IPv4 checksums, and rejection of DNS responses from an
  unexpected source.
- Host AddressSanitizer and UndefinedBehaviorSanitizer tests pass.

## Acceptance evidence

- Host and AddressSanitizer/UndefinedBehaviorSanitizer tests pass.
- x86-64 and AArch64 QEMU EFI guests both execute the self-test successfully.
- G03, G04, G05 and G06 regression gates pass after G07 integration.
- Gate output, source/build hashes and state are persisted in
  `logs/G07_RESULT.json`, `logs/G07_SHA256.txt` and `build/gate_state.json`.

The guest test driver is intentionally a deterministic in-memory device, not a
QEMU network card or physical NIC. No LAN, Internet, Raspberry Pi, or other
external system is contacted. TCP validation covers handshake/control-segment
behavior, not a complete stream/socket API or sustained data transfer.
Hardware networking remains unverified.
