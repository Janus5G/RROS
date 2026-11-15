# Private first-stage release

The owner authorized private repository creation, commits, pushes, CI execution
and private prerelease creation at **Janus5G/RROS**. The repository must remain
**PRIVATE**. Public access, external collaborators and download distribution
remain unauthorized. Licensing, reference-material rights and firmware
redistribution are still unresolved public-distribution blockers.

## Release safeguards

1. Verify repository privacy before each push, artifact upload or release change.
2. Commit only the focused release/docs/testing changes; preserve unrelated
   dirty files, existing release artifacts and accepted evidence.
3. Run full isolated preparation from the committed candidate. Require software
   PASS, exact checksums and a clean source snapshot. No QEMU result upgrades
   HARDWARE_NOT_VERIFIED.
4. Push only the authorized private repository, then run and inspect Actions.
   A validated workflow definition alone is not a GitHub-hosted test result.
5. Create a tagged prerelease, not a stable release. Its publication inside a
   private repository must never change repository visibility.
6. Verify the published private assets against the tag's fresh CI build.
7. Do not invite testers/collaborators or make the repository public.

## Exact PowerShell commands

These commands describe the authorized private stage. Use the actual reviewed,
committed and tested candidate and its fresh output directory:

```powershell
$Repository = 'Janus5G/RROS'
$Tag = 'v0.1.0-experimental.1'
$Assets = 'E:\OSBuild\build\public-release\0.1.0-experimental.1'

function Assert-PrivateRepository {
    $Info = gh repo view $Repository --json isPrivate,visibility | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0 -or -not $Info.isPrivate -or $Info.visibility -ne 'PRIVATE') {
        throw 'Repository is not verified PRIVATE; refusing upload/publication.'
    }
}

Assert-PrivateRepository
git push origin HEAD
gh workflow run experimental-release.yml --repo $Repository --ref main
gh run list --repo $Repository --workflow experimental-release.yml --limit 5
gh run watch RUN_ID --repo $Repository --exit-status

$Commit = git rev-parse HEAD
git tag -a $Tag $Commit -m 'RROS 0.1.0-experimental.1 private prerelease'
Assert-PrivateRepository
git push origin $Tag

$Names = @(
  'RROS-0.1.0-experimental.1-x86_64.tar.gz',
  'RROS-0.1.0-experimental.1-aarch64-qemu.tar.gz',
  'RROS-0.1.0-experimental.1-Pi5-probe.tar.gz',
  'RROS-0.1.0-experimental.1-SDK.tar.gz',
  'RROS-0.1.0-experimental.1-DevDeploy-resources.tar.gz',
  'RROS-0.1.0-experimental.1-source.tar.gz',
  'RROS-0.1.0-experimental.1-evidence.tar.gz',
  'release-manifest.json',
  'SHA256SUMS'
)
$Files = $Names | ForEach-Object { Join-Path $Assets $_ }
$Files | ForEach-Object {
    if (-not (Test-Path -LiteralPath $_)) { throw "Missing asset: $_" }
}
Assert-PrivateRepository
gh release create $Tag @Files --repo $Repository --verify-tag --draft --prerelease --title 'RoboticRasOS (RROS) 0.1.0-experimental.1 - private prerelease' --notes-file 'E:\OSBuild\docs\release\RELEASE_NOTES.md'
gh release view $Tag --repo $Repository --json tagName,isDraft,isPrerelease,assets,url
Assert-PrivateRepository
gh release edit $Tag --repo $Repository --draft=false --prerelease
gh run list --repo $Repository --workflow experimental-release.yml --limit 5
gh run watch RELEASE_RUN_ID --repo $Repository --exit-status
Assert-PrivateRepository
```

GitHub Actions uploads test artifacts only; it has read-only repository
permissions and does not create/edit releases. The release-published run
downloads actual private assets using the repository token and verifies them
against the event commit and fresh deterministic native build.

Any failed test remains visible. Diagnose release/testing infrastructure only;
do not reopen general OS development or the seven deferred findings. Public
distribution requires a new explicit owner decision and licensing clearance.
