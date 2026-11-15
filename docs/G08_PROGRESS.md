# G08 graphics and window manager progress

- Gate status: `SOFTWARE_PASS`
- Hardware status: `HARDWARE_NOT_VERIFIED`

## Planned implementation

Use UEFI GOP to capture the framebuffer metadata before handoff, then exercise
a bounded RGB/BGR software renderer, basic text/UI primitives, a two-window
compositor, and pointer-event handling for activation, drag and close after
ExitBootServices. Host tests use an allocated buffer; QEMU guests use the
firmware GOP framebuffer and a QMP-injected key to enter the test path.

## Acceptance evidence

- Host and AddressSanitizer/UndefinedBehaviorSanitizer tests pass for RGB/BGR
  pixels, bounds, composition, text, window activation/drag/close and input
  state.
- x86-64 and AArch64 QEMU guests use GOP framebuffer metadata captured before
  ExitBootServices, receive a QMP-injected key, then render and verify
  framebuffer contents after handoff.
- G08 output, source/build hashes and gate status are persisted in
  `logs/G08_RESULT.json`, `logs/G08_SHA256.txt` and `build/gate_state.json`.

The keyboard trigger is a QMP-injected firmware input; pointer gestures are
deterministic test events, not native mouse-driver input. QEMU results do not
establish Raspberry Pi 5 display, HDMI, or native input-driver operation.
