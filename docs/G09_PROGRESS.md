# G09 native application ABI and SDK progress

- Gate status: `SOFTWARE_PASS`
- Hardware status: `HARDWARE_NOT_VERIFIED`

## Planned implementation

Define a versioned, bounded native application image format with explicit
architecture, ABI, entry point, data size, capabilities and image checksum.
Validate it before copying into executable storage, keep writable data
separate, and connect SDK syscall wrappers to the G05 user/kernel ABI. Use the
existing guest process/isolation path to execute a loaded example image on
x86-64 and AArch64.

## Acceptance evidence

- Host tests cover valid x86-64/AArch64 images, ABI/architecture/version
  mismatches, malformed sizes/entry points, unsupported capabilities,
  checksum tampering, code/data overlap and destination immutability on
  rejection. AddressSanitizer and UndefinedBehaviorSanitizer pass.
- The native SDK example cross-compiles for both targets.
- x86-64 CPL3 and AArch64 EL0 guests execute the validated image through the
  existing process isolation/syscall path.
- Gate output, source/build hashes and state are persisted in
  `logs/G09_RESULT.json`, `logs/G09_SHA256.txt` and `build/gate_state.json`.

The initial image container is an internal software format, not ELF or a
signed distribution package. Its checksum detects accidental corruption but
does not authenticate a publisher. The guest application is built into the
isolated test fixture rather than loaded from persistent storage.
