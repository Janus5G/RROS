<#
.SYNOPSIS
  Guarded raw-image writer for Pi 5 validation media. DRY-RUN BY DEFAULT.
.DESCRIPTION
  1. Verifies the image SHA-256 against an expected value (release manifest or package manifest).
  2. Lists ONLY removable candidate disks (BusType USB or SD; never the system/boot disk; size bounded).
  3. Without -Execute nothing is written, even if -DiskNumber is given.
  4. With -Execute the operator must type an exact confirmation phrase built from the disk number, model and size.
  5. After writing, reads the media back and compares the SHA-256 of the first <image length> bytes.
  -TargetFile writes to a regular file instead of a disk to rehearse the copy+verify logic safely.
  The real-disk write path has NOT been executed in this project (no physical media was authorized).
.EXAMPLE
  .\write_image_safe.ps1                                   # list candidates, verify the image, write nothing
  .\write_image_safe.ps1 -DiskNumber 3                     # show what would be erased, write nothing
  .\write_image_safe.ps1 -DiskNumber 3 -Execute            # prompts for the confirmation phrase, then writes
  .\write_image_safe.ps1 -TargetFile C:\temp\rehearsal.img -Execute   # file rehearsal
#>
[CmdletBinding()]
param(
    [string]$Image,
    [string]$ExpectedSha256,
    [int]$DiskNumber = -1,
    [string]$TargetFile,
    [switch]$Execute,
    [long]$MaxDiskBytes = 256GB
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $Image) { $Image = Join-Path $root 'build\pi5-hw-validation\release-snapshot\ExperimentalOS-Pi5.img' }
if (-not (Test-Path -LiteralPath $Image)) { throw "Image not found: $Image (run: python scripts/pi5_hw/build_package.py)" }
$Image = (Resolve-Path -LiteralPath $Image).Path

if (-not $ExpectedSha256) {
    $sums = Join-Path $root 'build\manifests\SHA256SUMS'
    $line = Get-Content $sums | Where-Object { $_ -match 'build/aarch64/ExperimentalOS-Pi5\.img\s*$' } | Select-Object -First 1
    if (-not $line) { throw 'Expected SHA-256 not found; pass -ExpectedSha256' }
    $ExpectedSha256 = ($line -split '\s+')[0]
}
$actual = (Get-FileHash -LiteralPath $Image -Algorithm SHA256).Hash.ToLower()
Write-Host "Image   : $Image"
Write-Host "SHA-256 : $actual"
if ($actual -ne $ExpectedSha256.ToLower()) { throw "Image hash mismatch (expected $ExpectedSha256). Refusing to continue." }
Write-Host 'Hash matches the expected release value.' -ForegroundColor Green
$imageLength = (Get-Item -LiteralPath $Image).Length

function Copy-AndVerify([System.IO.Stream]$target, [string]$imagePath, [long]$length, [string]$expected) {
    $buffer = New-Object byte[] (1MB)
    $src = [System.IO.File]::OpenRead($imagePath)
    try {
        while (($n = $src.Read($buffer, 0, $buffer.Length)) -gt 0) {
            # Raw devices require sector-multiple writes; the image length is a multiple of 512.
            $target.Write($buffer, 0, $n)
        }
        $target.Flush()
    } finally { $src.Dispose() }
    $target.Position = 0
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $remaining = $length
    while ($remaining -gt 0) {
        $want = [int][Math]::Min($buffer.Length, $remaining)
        $got = $target.Read($buffer, 0, $want)
        if ($got -le 0) { throw 'Short read during verification' }
        [void]$sha.TransformBlock($buffer, 0, $got, $null, 0)
        $remaining -= $got
    }
    [void]$sha.TransformFinalBlock($buffer, 0, 0)
    $readback = ([BitConverter]::ToString($sha.Hash) -replace '-', '').ToLower()
    if ($readback -ne $expected.ToLower()) { throw "Read-back verification FAILED ($readback)" }
    Write-Host "Read-back SHA-256 verified: $readback" -ForegroundColor Green
}

if ($TargetFile) {
    if (-not $Execute) { Write-Host "DRY RUN: would write to file $TargetFile. Add -Execute to rehearse."; return }
    if ((Test-Path -LiteralPath $TargetFile) -and ((Get-Item -LiteralPath $TargetFile).PSIsContainer)) { throw 'TargetFile is a directory' }
    $fs = [System.IO.File]::Open($TargetFile, 'Create', 'ReadWrite')
    try { Copy-AndVerify $fs $Image $imageLength $actual } finally { $fs.Dispose() }
    Write-Host "Rehearsal complete: $TargetFile"
    return
}

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
$all = Get-Disk
$candidates = $all | Where-Object {
    $_.BusType -in @('USB', 'SD', 'MMC') -and -not $_.IsSystem -and -not $_.IsBoot -and
    $_.Size -ge $imageLength -and $_.Size -le $MaxDiskBytes -and $_.OperationalStatus -ne 'Offline'
}
Write-Host "`nAll disks (for orientation; only the marked ones are eligible):"
$all | ForEach-Object {
    $eligible = $candidates.Number -contains $_.Number
    '{0}  #{1,-2} {2,-28} {3,-6} {4,8:N1} GB  system={5} boot={6}' -f ($(if ($eligible) { '[ELIGIBLE]' } else { '[ blocked ]' })),
        $_.Number, $_.FriendlyName, $_.BusType, ($_.Size / 1GB), $_.IsSystem, $_.IsBoot
}
if ($DiskNumber -lt 0) { Write-Host "`nNo -DiskNumber given: nothing will be written."; return }

$disk = $candidates | Where-Object { $_.Number -eq $DiskNumber }
if (-not $disk) { throw "Disk $DiskNumber is not an eligible removable disk. Refusing." }
$phrase = 'ERASE DISK {0} {1} {2}GB' -f $disk.Number, ($disk.FriendlyName -replace '\s+', '_'), [Math]::Round($disk.Size / 1GB)
Write-Host "`nTARGET: #$($disk.Number) $($disk.FriendlyName) serial=$($disk.SerialNumber) size=$([Math]::Round($disk.Size/1GB,1)) GB"
Get-Partition -DiskNumber $disk.Number -ErrorAction SilentlyContinue | ForEach-Object {
    "  existing partition {0}: {1:N1} GB {2}" -f $_.PartitionNumber, ($_.Size / 1GB), $_.Type }
if (-not $Execute) { Write-Host "`nDRY RUN: nothing written. Re-run with -Execute to proceed."; Write-Host "Confirmation phrase would be: $phrase"; return }
if (-not $isAdmin) { throw 'Writing a raw disk requires an elevated (Administrator) session.' }
$typed = Read-Host "Type exactly '$phrase' to erase this disk"
if ($typed -cne $phrase) { throw 'Confirmation phrase mismatch. Nothing written.' }

Set-Disk -Number $disk.Number -IsOffline $true
try {
    $fs = New-Object System.IO.FileStream("\\.\PhysicalDrive$($disk.Number)", 'Open', 'ReadWrite', 'None', 1MB, [System.IO.FileOptions]::WriteThrough)
    try { Copy-AndVerify $fs $Image $imageLength $actual } finally { $fs.Dispose() }
} finally { Set-Disk -Number $disk.Number -IsOffline $false }
Write-Host 'Write and verification complete. Eject the media safely before moving it to the Pi.'
