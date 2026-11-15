# G19 Progress — Graphics Acceleration and Optimization

Status: SOFTWARE_PASS (software renderer only). Raspberry Pi 5 GPU: HARDWARE_NOT_VERIFIED.

## Implemented (`kernel/gfx.{h,c}`, freestanding, no allocation)

- Damage tracking: at most 8 rects per frame, clipped, merged, bounding-box fallback on overflow.
- Buffer reuse: 3-buffer output pool with a 4-frame damage history and buffer-age repaint; a full repaint is used only for invalid or too-old buffers.
- Frame synchronization: a submitted frame becomes visible only at vblank; a newer submit supersedes an unpresented one (counted as `dropped`); fences signal when `seq <= shown_seq`.
- Multi-display architecture: up to 4 outputs with origins and overlap/limit validation, up to 16 z-ordered layers, per-output damage translation.
- OpenGL-compatible path: a bounded GLES-like command stream (clear, scissor, rect, triangle), fully validated before any pixel is written, executed by a software rasterizer.
- GPU transport abstraction: `gfx_backend_software` and a `gfx_backend_v3d_unavailable` stub that always returns `GFX_ERR_NO_HARDWARE` and writes nothing. Fallback to software happens only when the preferred backend reports no hardware; a hardware backend is never the fallback.
- `gfx_api_support` reports honestly: gles2-subset-software available; raspberry-pi-v3d, vulkan, opengl-conformant unavailable.

## Measurements

Correctness: 300 random layer operations (including dropped frames) give byte-identical frames between the damage-tracked system and a full-repaint reference.

- Host (not guest): 640x480 cursor workload over 300 frames wrote 911,535 pixels with damage tracking vs 165,283,200 for full repaint (181.3x fewer). Timings are informational only.
- Guest (QEMU x86_64 and aarch64, 256x160, 200 frames): damage 172,226 vs full 10,215,200 pixels. Tick counts are printed from the emulated guest counter (`G19 MEASURE` serial line, recorded in the gate log). They are relative figures under emulation, not Raspberry Pi 5 performance.

## Not implemented / limits

- Not implemented: a Raspberry Pi 5 V3D driver. HVS/HDMI are disabled in the pinned device tree and no verified V3D, mailbox or MMIO interface exists, so the transport is a fail-closed stub.
- Not implemented: Vulkan (remains an optional future subsystem until a backend exists).
- The GLES subset is not conformant OpenGL/GLES; it is a software rasterizer only (compatibility-pending).
- Multi-display is a software layout model with no real scanout hardware; vblank is caller driven, not a hardware interrupt.
- The pipeline is not yet wired into `graphics.c`, `desktop.c` or the UEFI GOP scanout; guest tests use a static buffer.
- All hardware behavior: HARDWARE_NOT_VERIFIED.

## Tests

`scripts/test-gfx-host.sh` (plain and ASan/UBSan), `tests/gates/G19_gfx_test.c`, `tests/gates/G19_test.py`, QEMU `scripts/run-qemu-input.py <arch> G19` on x86_64 and aarch64.
