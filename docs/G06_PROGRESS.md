# G06 storage and filesystem progress

- Gate status: `SOFTWARE_PASS`
- Hardware status: `HARDWARE_NOT_VERIFIED`
- Acceptance report: `logs/G06_RESULT.json`
- Artifact hashes: `logs/G06_SHA256.txt`

## Current implementation

- Shared fixed-capacity file APIs operate over a bounded 512-byte block-device
  interface.
- The filesystem uses two checksummed metadata snapshots and copy-on-write data
  extents. A snapshot is considered committed only after its final metadata
  sector is written, allowing a previous snapshot to recover from an interrupted
  metadata update.
- File paths, extents, sizes, duplicate entries and overlapping allocations are
  validated. File reads verify per-file data checksums.
- QEMU guest self-tests use an in-memory block device; host acceptance tests use
  only `build/g06/disposable-volume.img`, a disposable file-backed test volume.

## Acceptance evidence

`python3 scripts/gates/run_gate.py G06` passed. Host tests use the disposable
file-backed volume to validate remount persistence, interrupted metadata
commit recovery, bounds, empty and multi-block files, deletion, and detection
of modified file data. The same shared implementation passes RAM-backed
filesystem read/write/remount checks in both QEMU UEFI guests. Host tests also
pass AddressSanitizer and UndefinedBehaviorSanitizer.

G03, G04 and G05 regression tests passed after storage was added to the builds.

## Scope and limitations

- Persistent-volume tests use only `build/g06/disposable-volume.img`; no
  physical media is accessed.
- Guest tests use an in-memory block device. No native physical or firmware
  block-device driver, production media support, or physical power-failure
  behavior is established.
- The filesystem is fixed-capacity and does not provide directories, file
  permissions, secure erase or cryptographic authenticity. Its FNV checksums
  detect accidental corruption only.
