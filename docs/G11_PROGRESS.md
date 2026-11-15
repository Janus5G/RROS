# G11 Flow desktop progress

Historical private logs referenced below are not included in this distribution.

- Gate status: `SOFTWARE_PASS`
- Hardware status: `HARDWARE_NOT_VERIFIED`

## Implemented and verified

- Added a dock-based Flow shell to the shared desktop renderer, with separate
  light/dark theme palettes.
- Dock selection switches Console, File Manager and Settings without creating
  duplicate processes/windows. Both shells reuse the same process/thread
  creation, scheduling, termination, reaping and rendering services.
- Verified theme rendering, app switching, resize, close and complete resource
  cleanup.
- Host/sanitizer checks and x86-64/AArch64 QEMU Flow tests passed. Classic
  desktop host and guest regressions passed in the same gate.

The machine-readable result and SHA-256 evidence are in
`../logs/G11_RESULT.json`,
`../logs/G11_RESULT.md` and
`../logs/G11_SHA256.txt`.

## Limits

The app windows remain lifecycle fixtures rather than executing native
applications. Guest activation is QMP-injected, while dock selection, theme
changes and other UI events are deterministic test events. Native display/input
drivers and Raspberry Pi hardware are not verified.
