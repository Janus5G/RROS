$ErrorActionPreference = "Continue"
Set-Location (Join-Path $PSScriptRoot "..")
$out = "build\audit"
New-Item -ItemType Directory -Force $out | Out-Null
$summary = Join-Path $out "run-summary.tsv"
"gate`tstart_utc`tend_utc`texit_code`thead" | Set-Content $summary
$head = (git rev-parse HEAD).Trim()
foreach ($n in 0..22) {
    $g = "G{0:D2}" -f $n
    $start = (Get-Date).ToUniversalTime().ToString("o")
    $log = Join-Path $out "$g.out.txt"
    & python scripts\gates\run_gate.py $g *> $log
    $code = $LASTEXITCODE
    $end = (Get-Date).ToUniversalTime().ToString("o")
    "$g`t$start`t$end`t$code`t$head" | Add-Content $summary
}
"DONE" | Add-Content $summary
