# One-command AArch64 QEMU/UEFI virtual-platform validation (not Raspberry Pi hardware verification).
# Resolves the Windows repository path to its WSL2 mount and runs the POSIX entry point there.
param([switch]$HostFixtures, [int]$Timeout = 40)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$wslRoot = (wsl --exec wslpath -a ($root -replace '\\', '/')).Trim()
if (-not $wslRoot) { throw "Could not resolve WSL path for $root" }
$flags = @('--timeout', $Timeout)
if ($HostFixtures) { $flags += '--host-fixtures' }
wsl --exec sh -c "cd '$wslRoot' && sh scripts/run_pi5_gate_test.sh $($flags -join ' ')"
exit $LASTEXITCODE
