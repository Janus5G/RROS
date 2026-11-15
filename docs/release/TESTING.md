# Reproducible release validation

## Evidence and isolation

Run [prepare.py](../../scripts/release/prepare.py) from the full owner-cleared
checkout on Linux x86-64 or WSL2. It creates a private temporary local clone at
the actual HEAD, overlays only release/source inputs, removes inherited build/
logs in that clone, and stages the snapshot there for input hashing. It makes
no original-branch/index changes and no commit, push or publication.

Original release images, manifests, SDK archives, gate reports and probe
packages are not rebuilt in place. Each candidate output is new; existing
output directories are refused. The source digest covers actual source bytes,
including uncommitted release work. Dirty candidates need a committed rerun
before publication.

The current complete checkout includes `OSBuild.pdf` for unchanged G00 tests.
Its redistribution rights are unresolved, so it is excluded from the public
source archive. A standalone public archive cannot reproduce G00 without an
owner-cleared copy. This is an explicit limitation, not a substituted PASS.

## Prerequisites

Existing host: Clang/LLD 21.1.8, QEMU 10.2.1, xorriso, C compiler and sanitizers,
Python 3, OVMF/AAVMF, pyserial 3.5 and Node.js for the pinned RoboForge test.
Use the [existing toolchain instructions](../TOOLCHAIN.md) on WSL.
Do not run the disposable-container provisioning script on a shared host.
For local workflow validation, provision the hash-pinned validator once with
`sh scripts/release/bootstrap-actionlint.sh`. It installs only under
`.toolchain/actionlint`, not globally. Actionlint validates both workflows;
shellcheck/pyflakes execution is not claimed.
Both workflows pin checkout v7.0.1 and upload-artifact v7.0.2 to their verified
commit SHAs. Their upstream `action.yml` declares Node.js 24. Repository
permissions, privacy checks, credential persistence and artifact defaults remain
unchanged; Node.js 20 actions are not retained or merely force-overridden.
The minimal CI container installs Git before checkout so the action creates
real Git metadata instead of silently falling back to a REST source archive.
Container Git also trusts only the exact disposable `$GITHUB_WORKSPACE` path;
it does not disable ownership checks globally with a wildcard.

CI uses the exact Ubuntu OCI digest in
[the workflow](../../.github/workflows/experimental-release.yml),
Ubuntu snapshot `20261113T000000Z` and pinned LLVM/QEMU/firmware packages.
The complete installed package inventory is uploaded. A missing/unavailable
pin fails setup instead of silently using a newer tool.

Read-only upstream sources on the owner's development host:

```sh
git init build/external/RoboForge
git -C build/external/RoboForge fetch --depth=1 https://github.com/Janus5G/RoboForge.git b0dc46f860faedb6f00a36b2c636a2b31c2b290e
git -C build/external/RoboForge checkout --detach FETCH_HEAD
git init build/external/ChromaNeural
git -C build/external/ChromaNeural fetch --depth=1 https://github.com/Janus5G/ChromaNeural.git 3ce209ccd2aeb52094fbaade4429632eb17d3340
git -C build/external/ChromaNeural checkout --detach FETCH_HEAD
```

Only do this in absent/new checkout directories, not over someone else's
work, and with owner-authorized access. Both upstream repositories are
currently PRIVATE. Neither tree is altered or bundled in release assets.
GitHub CI uses its repository-scoped token, not a copied personal credential;
it cannot read these other private repositories. CI explicitly records
G15 live-upstream and G16 upstream cross-check as NOT_TESTABLE, while running
the pinned local contract/vector fixtures. The full upstream checks are run
locally using the existing authorized checkouts. No skip is advertised as PASS.

Independently fetch the pinned firmware for local tests:

```sh
mkdir -p build/pi5-boot-chain-cache
curl --fail --location --output build/pi5-boot-chain-cache/RPi5_UEFI_Release_v0.3.zip https://github.com/worproject/rpi5-uefi/releases/download/v0.3/RPi5_UEFI_Release_v0.3.zip
echo '2337df318845448ebcc8a6a587d15dccec44b54c665ece21250d2f6170732724  build/pi5-boot-chain-cache/RPi5_UEFI_Release_v0.3.zip' | sha256sum -c -
```

Firmware is needed for host parser/probe packaging tests, not physically booted
by GitHub CI. Third-party firmware files are removed from the public probe
archive; package checksums are recomputed over exactly the distributed files.

## Run and inspect

From Windows PowerShell:

```powershell
wsl --exec sh -lc 'cd /mnt/e/OSBuild && python3 scripts/release/check.py'
wsl --exec sh -lc 'cd /mnt/e/OSBuild && python3 scripts/release/prepare.py'
```

Or from the Linux repository root:

```sh
python3 scripts/release/check.py
python3 scripts/release/prepare.py
cd build/public-release/0.1.0-experimental.1
sha256sum -c SHA256SUMS
```

`--output <new-directory>` creates another candidate without overwriting any
previous result. `--tooling-only` runs only self-tests/console fixtures and
records overall release status NOT_TESTABLE, never a full release PASS.

## Stages and coverage

The release workflow first runs seven independent matrix checks for CR-01
through CR-07. Each uploads its actual PASS/FAIL/NOT_TESTABLE JSON, source commit,
input hashes and explicit HARDWARE_NOT_VERIFIED classification. Failed checks
block the release job; no finding is resolved merely because a fix is committed.
The targeted runner performs no board operations and no G00-G22 builds:

```sh
python3 tests/pi5_hw_validation/test_review_regressions.py --report build/code-review/results.json
gh workflow run experimental-release.yml --repo Janus5G/RROS --ref main -f review_only=true
```

`review_only=true` skips gate audits, OS/probe builds and candidate packaging,
but checks container checkout, tool provisioning, documentation/workflow syntax
and the six release-infrastructure self-tests. It produces evidence, not a
release candidate. Use this mode instead of restarting the audit locally.

| Finding | Targeted regression |
|---|---|
| CR-01 | Unsafe labels fail before imports/device access or output creation; safe mocked capture remains inside its directory |
| CR-02 | Real probe-package producer, then copied standalone verifier without original checkout; tamper rejection |
| CR-03 | Legacy producer and copied standalone verifier with included structural checker; GPT/FAT/PE, image mismatch and unsafe manifest rejection |
| CR-04 | Complete producer result vocabulary, truncated/unknown results, contradictory counters/results |
| CR-05 | Short/padded uppercase/lowercase hex; exact Pi debug UART value; malformed/wrong/overflow addresses |
| CR-06 | Packaged self-test, port listing, probe evaluator and verifier execute as documented |
| CR-07 | Duplicate output, conflicting PASS/FAIL in either order, missing markers/checks, Pi identity mismatch, authentic generic QEMU sample |

Pi and package-image fixtures are explicitly synthetic, not physical evidence.
The QEMU marker fixture is an authentic excerpt of
`logs/pi5_probe/20261113T120113Z_qemu_probe_serial.log`, retained under
`tests/pi5_hw_validation/fixtures/`. Its RP1 FAIL is informational on generic
QEMU; rejecting it as a software failure would incorrectly invalidate accepted
evidence. Only the two known Pi-only discovery checks are optional in QEMU mode.
Parser PASS does not authenticate the submitted log or establish native
peripheral functionality.

### Verified targeted CI evidence

Historical private Actions run `43456276606` tested commit
`e3ff9e6a8ebd1f06f2a668b86f76e9f313d03fc6`; its run page and artifacts are
not included in this distribution:
**CR-01 PASS, CR-02 PASS, CR-03 PASS, CR-04 PASS, CR-05 PASS, CR-06 PASS,
CR-07 PASS** (nine test methods plus parameterized negative cases).
All seven downloaded per-finding reports record clean source inputs at that
commit. The same new regressions failed for all seven groups against unfixed
HEAD `16f04163835bf4d29a4f5408daaa42c8cdbfba1c` in an isolated local clone.
The seven findings are therefore software-verified corrections, not cancelled.

The release-infrastructure job also passed exact Git checkout, pinned
provisioning, documentation/actionlint checks and six infrastructure self-tests.
All eight checks had zero annotations; complete run logs had zero Node.js 20
deprecation warnings. The normal gate/build/packaging and published-attachment
steps were intentionally skipped in review-only mode: this is **not** a full
release-candidate PASS and no release was published by this run.

Independent historical native-ARM64 run `43456275355` passed its four
executed host suites at the same code commit; its run page and artifacts are
not included in this distribution. Combined native G12 remains
NOT_TESTABLE at hosted EL0; physical Raspberry Pi 5 remains HARDWARE_NOT_VERIFIED.

1. Gate-runner UTF-8/history self-tests; deterministic archive/source-digest
   tests; hardware-tool self-tests; plain and ASan/UBSan console tests using
   the real pinned Pi DTB.
2. Existing gate runner G00-G20 **once each**, preserving full JSON/Markdown/
   hashes and exact underlying command logs. Includes CPU/ABI/process,
   storage/network, graphics/desktop, driver, AI, MCP, compatibility, runtime,
   browser and security simulation checks. Gate builds retain their required
   architecture/test flags; unrelated builds are not added.
3. G21 runs integration, dependency audit and two clean EFI builds. Its
   `--reuse-regressions` mode verifies the preceding G00-G20 reports' HEAD,
   PASS/exit codes and recorded input hashes instead of rebuilding the same
   regression suite. Default standalone G21 behavior remains unchanged.
4. Two existing clean release builds run **only inside the private clone**.
   All six native/image/SDK/resource payload hashes must match exactly.
   G22 `--check-existing` then runs the existing image negative tests, ISO
   integrity, manifests, SDK/resources and actual QEMU boots without another
   pair of release builds. Its default standalone behavior remains unchanged.
5. The existing UEFI harness tests cortex-a72/a76, GICv2/v3, 4-CPU/512 MiB,
   attached generic PCI/USB/storage devices and marker determinism. Its new
   `--driver-esp` selects an isolated saved G12 fixture ESP; manifest checking
   still refers to fresh release EFI files. Read-only overlays protect images.
   Device attachment proves boot unaffected, not native device support.
6. Probe build, generic QEMU run/evaluation and existing probe package builder.
   Public archives contain authentic probe code/logs/tools, not bundled firmware.
7. Packaging uses sorted paths, fixed modes/mtime/owners and gzip mtime zero.
   Per-file SHA-256 and source snapshot digest are recorded. Unsigned artifacts
   are not described as authenticated.

## Outcome interpretation

PASS and FAIL are actual executed software outcomes. NOT_TESTABLE is recorded
for physical subsystems and unavailable optional source cross-checks. A required
firmware/tool fetch failure is a setup FAIL, not a dependency PASS. The manifest always
records HARDWARE_NOT_VERIFIED. Local execution records GitHub Actions
NOT_EXECUTED; workflow syntax validation is not a hosted-run result.

On `release: published`, the workflow also uses `gh release download` to
verify the **actually attached** assets: complete checksums, archive safety,
native payload hashes, clean source commit and source snapshot equality against
the fresh event build. It fails for missing, tampered or dirty-source releases.

The independent [Native ARM64 software tests](../../.github/workflows/native-arm64.yml)
run G03/G05/G06/G07 host/sanitizer suites. The combined G12 suite is explicitly
NOT_TESTABLE at hosted EL0 because its cache-maintenance path includes
privileged AArch64 instructions. It is not executed on that runner. Native ARM64
on a hosted machine is not BCM2712/RP1 hardware validation.

## Known unresolved test-tool limitations

- Historical `build/*/esp` differs from old manifest entries because earlier
  gates shared output paths. The image/ISO/SDK remain accepted evidence.
  This is not repaired or silently rehashed; new isolated builds avoid reuse.
- Historical G21/G22 nested builds are redundant; default behavior remains
  available. The guarded historical release builder cannot be rerun over
  original images without owner approval. Isolation, not disabling its guard,
  is used for new candidates.
- Probe parsing now checks complete END results, reconciles de-duplicated
  counters and rejects unexpected FAIL checks. Physical testers must still
  inspect and retain the full capture; parser success is not authentication.
- Legacy serial evaluator self-test can PASS a synthetic complete marker log.
  Its old baseline requires NOT_PASS because no G12 fail-closed marker exists.
  Parser success is not authentic physical evidence.
- G18 contains intentional unsupported-format SKIP messages inside host tests.
  A gate PASS does not imply PNG/JPEG/GIF decoding or full web compatibility.
- Load-related QEMU timeouts are recorded failures unless all required markers
  satisfy the harness. Existing configurable timeouts are used; assertions
  are not weakened.
- CR-01 through CR-07 are covered by the targeted checks above, not cancelled.
  Resolution requires their actual GitHub Actions checks to pass.
- Initial private native-ARM64 run `43445380900` genuinely failed G12 with
  exit 132 (`Illegal instruction`). That evidence is preserved, not relabelled.
  G12 x86 register models and privileged AArch64 QEMU fixtures remain required.
- A fresh private clone exposed missing QEMU log/UEFI-variable directories in
  the basic boot runners. They now create their log directories before use;
  guest marker assertions are unchanged.
- Initial release CI run `43446518628` failed trying to fetch private upstream
  references without cross-repository authority. Its failure is retained.
  CI now reports that access boundary as NOT_TESTABLE; no personal token is
  copied, no upstream repository is made public and no fixture-only result is
  labelled a live upstream test.
- Node24 run `43447861217` removed the Node.js 20 warning but exposed the
  minimal-container checkout archive fallback. Git is now installed first and
  a real HEAD is required.
- Run `43452353274` subsequently failed Git's container ownership check before
  tests; its failure is retained. The exact disposable workspace is now trusted
  explicitly. The early privacy check also requires the pinned `gh` bootstrap.
- The fresh local G21 audit found the standalone host console test under
  `kernel/`. The audit now verifies its host-runner binding and absence from
  all three firmware build scripts before excluding that host-only file from
  the freestanding-runtime header audit. Runtime sources remain checked.

See [release readiness](../RELEASE_READINESS.md) and the
[physical testing guide](../../RASPBERRY_PI5_TEST_GUIDE.md).
