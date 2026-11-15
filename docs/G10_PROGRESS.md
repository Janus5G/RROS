# G10 Classic desktop progress

Historical private logs referenced below are not included in this distribution.

- Gate status: `SOFTWARE_PASS`
- Hardware status: `HARDWARE_NOT_VERIFIED`

## Implemented and verified

- Added the Classic taskbar, launcher, and Console, File Manager and Settings
  windows using the G08 framebuffer renderer.
- Launch creates a process and runnable thread record per application, with
  separate W^X code, data and stack virtual regions. Close terminates/reaps
  those records and verifies resource cleanup.
- The test exercises launcher input, application launch, resize, close and
  taskbar/window framebuffer output.
- x86-64 and AArch64 QEMU guests use GOP framebuffer output after
  `ExitBootServices`; QMP injects the activation key. Remaining UI actions are
  deterministic guest-side test events.

## Acceptance evidence

- Host tests and AddressSanitizer/UndefinedBehaviorSanitizer passed.
- x86-64 and AArch64 QEMU desktop and lifecycle tests passed.
- G03-G09 regression acceptance passed after G10 integration.
- Machine-readable output, commands, build/source hashes and logs are in
  `../logs/G10_RESULT.json`,
  `../logs/G10_RESULT.md` and
  `../logs/G10_SHA256.txt`.

## Limits

The three app windows are shell lifecycle fixtures, not independently loaded
or executing native applications. Keyboard activation is QMP-injected; the
remaining UI actions are deterministic test events, not native input handling.
The framebuffer is firmware-provided GOP in QEMU; no Raspberry Pi display,
input or hardware behavior is verified.
