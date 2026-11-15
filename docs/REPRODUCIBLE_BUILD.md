# Reproducible build instructions

For new public-facing candidates use [isolated release preparation](release/TESTING.md).
The historical command below refuses to overwrite an existing release image;
do not opt into an in-place rebuild without explicit owner approval.
New candidates run it only inside a private temporary clone and preserve the
accepted original artifacts.

Host: Windows with WSL2 (Ubuntu). All commands run in WSL from the repository root. No network access is
needed after the local toolchain (`.toolchain/`, clang/lld) and QEMU/OVMF are present.

```sh
sh scripts/bootstrap-toolchain.sh      # once; provisions the local clang/lld toolchain
sh scripts/build-release.sh            # clean build of both arches, images, SDK, manifests
sha256sum -c build/manifests/SHA256SUMS
```

From Windows PowerShell: `wsl --exec sh -c "cd /mnt/e/OSBuild && sh scripts/build-release.sh"`.

Reproducibility: the linker runs with `/brepro` (no PE timestamps), `SOURCE_DATE_EPOCH=1767225600`, archives
use sorted names and fixed owners/mtimes, and FAT/GPT images are produced by the deterministic
`tools/mkimage.py`. `tests/gates/G21_test.py` and `tests/gates/G22_test.py` compare two clean builds
bit-for-bit. Manifests record the source commit and a digest of all tracked sources; a manifest with
`source_tree_dirty: true` does not correspond to a committed source state.

Testing: `python scripts/gates/run_gate.py G22` runs the gate and writes evidence under `logs/`.
QEMU: `sh scripts/run-qemu-x86_64.sh`, `sh scripts/run-qemu-aarch64.sh`. Debug logs: serial logs under
`build/<arch>/qemu-serial.log`.
