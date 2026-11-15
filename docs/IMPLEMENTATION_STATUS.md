# Implementation status

**Last updated:** 2026-11-13 (G00-G22 independently re-audited at 3ad5ece)

| Gate | Software state | Hardware state | Evidence |
|---|---|---|---|
| G00 | `SOFTWARE_PASS` | `HARDWARE_NOT_VERIFIED` | `logs/G00_RESULT.json`; x86-64 Clang/LLD, QEMU and OVMF preparation verified |
| G01 | `SOFTWARE_PASS` | `HARDWARE_NOT_VERIFIED` | `logs/G01_RESULT.json`; actual QEMU UEFI handoff, serial, console and idle checks |
| G02 | `SOFTWARE_PASS` | `HARDWARE_NOT_VERIFIED` | `logs/G02_RESULT.json`; native AArch64 EFI image, QEMU UEFI handoff, synchronous exception vector, MMU state and memory-map checks |
| G03 | `SOFTWARE_PASS` | `HARDWARE_NOT_VERIFIED` | `logs/G03_RESULT.json`; host allocator tests and x86-64/AArch64 guest tests for reserved pages, ownership, heap, virtual ranges and fault records |
| G04 | `SOFTWARE_PASS` | `HARDWARE_NOT_VERIFIED` | `logs/G04_RESULT.json`; x86-64/AArch64 UEFI timer events, architecture counter progression, device checks, QMP-injected USB key input and safe handoff |
| G05 | `SOFTWARE_PASS` | `HARDWARE_NOT_VERIFIED` | `logs/G05_RESULT.json`; host tests and sanitizer run plus real x86-64 CPL3 and AArch64 EL0 guest syscall, isolation, scheduling and cleanup tests |
| G06 | `SOFTWARE_PASS` | `HARDWARE_NOT_VERIFIED` | `logs/G06_RESULT.json`; disposable file-backed reopen/recovery, sanitizers and x86-64/AArch64 QEMU RAM-filesystem tests |
| G07 | `SOFTWARE_PASS` | `HARDWARE_NOT_VERIFIED` | `logs/G07_RESULT.json`; host/sanitizer and x86-64/AArch64 QEMU guest protocol tests; G03-G06 regressions pass |
| G08 | `SOFTWARE_PASS` | `HARDWARE_NOT_VERIFIED` | `logs/G08_RESULT.json`; host/sanitizer and x86-64/AArch64 QEMU GOP framebuffer/window/input tests |
| G09 | `SOFTWARE_PASS` | `HARDWARE_NOT_VERIFIED` | `logs/G09_RESULT.json`; loader/sanitizer tests, SDK x86-64/AArch64 builds and CPL3/EL0 loaded-app isolation tests |
| G10 | `SOFTWARE_PASS` | `HARDWARE_NOT_VERIFIED` | `logs/G10_RESULT.json`; host/sanitizer and x86-64/AArch64 QEMU desktop, lifecycle and regression tests |
| G11 | `SOFTWARE_PASS` | `HARDWARE_NOT_VERIFIED` | `logs/G11_RESULT.json`; host/sanitizer and x86-64/AArch64 Flow and Classic shared-service tests |
| G12 | `SOFTWARE_PASS` (conditional) | `HARDWARE_NOT_VERIFIED` | `logs/G12_RESULT.json`; host/ASan/UBSan fixtures and QEMU guests. Conditional: Wi-Fi lifecycle/firmware, camera data path, DWC3 startup, HDMI mode programming, GEM production startup and audio production wiring remain fail-closed/inactive; see `docs/FINAL_GATE_VERIFICATION_REPORT.md` |
| G13 | `SOFTWARE_PASS` | `HARDWARE_NOT_VERIFIED` | `logs/G13_RESULT.json`; see `docs/G13_PROGRESS.md` |
| G14 | `SOFTWARE_PASS` | `HARDWARE_NOT_VERIFIED` | `logs/G14_RESULT.json`; see `docs/G14_PROGRESS.md` |
| G15 | `SOFTWARE_PASS` | `HARDWARE_NOT_VERIFIED` | `logs/G15_RESULT.json`; see `docs/G15_PROGRESS.md` |
| G16 | `SOFTWARE_PASS` | `HARDWARE_NOT_VERIFIED` | `logs/G16_RESULT.json`; see `docs/G16_PROGRESS.md` |
| G17 | `SOFTWARE_PASS` | `HARDWARE_NOT_VERIFIED` | `logs/G17_RESULT.json`; see `docs/G17_PROGRESS.md` |
| G18 | `SOFTWARE_PASS` | `HARDWARE_NOT_VERIFIED` | `logs/G18_RESULT.json`; see `docs/G18_PROGRESS.md` |
| G19 | `SOFTWARE_PASS` | `HARDWARE_NOT_VERIFIED` | `logs/G19_RESULT.json`; see `docs/G19_PROGRESS.md` |
| G20 | `SOFTWARE_PASS` | `HARDWARE_NOT_VERIFIED` | `logs/G20_RESULT.json`; see `docs/G20_PROGRESS.md` |
| G21 | `SOFTWARE_PASS` | `HARDWARE_NOT_VERIFIED` | `logs/G21_RESULT.json`; see `docs/G21_PROGRESS.md` |
| G22 | `SOFTWARE_PASS` | `HARDWARE_NOT_VERIFIED` | `logs/G22_RESULT.json`; see `docs/G22_PROGRESS.md` |

G05 now verifies real x86-64 and AArch64 user-mode transitions, architecture
exception/syscall context switching, process isolation, cooperative scheduling,
fault containment, process/thread termination and resource cleanup in QEMU.
The host process model and its adversarial/concurrency tests also pass under
AddressSanitizer and UndefinedBehaviorSanitizer. These fixed-capacity,
single-CPU guest tests do not establish timer-preemptive or multicore scheduling.
Historical private gate state and logs are not included in this distribution.
The table records earlier software claims, not a fresh verification; planned
features are not implemented features.
