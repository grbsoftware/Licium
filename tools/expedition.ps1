# expedition.ps1 — the expedition mixer's CLI ancestor (docs\LATENT_GROWTH_DESIGN.md).
#
# Splits a seed budget AND the machine's cores across search methods, runs
# each method as its own latent_prospect process (own core count via
# --threads, own DISJOINT seed slice, own emit share), waits, and reports.
# Because every slice is an ordinary scan, all determinism/lineage rules
# hold exactly as documented — the mixer is a scheduler, not a search mode.
#
# Usage:
#   .\expedition.ps1 -Start 20000000 -Budget 3000000 -Emit 60 -Mix "cold=60,threads=20,swarm=20"
#   .\expedition.ps1 -Start 20000000 -Budget 1000000 -Emit 40 -Mix "threads=100"
#   Mix values are PERCENTAGES of both the seed budget and the worker cores
#   (cores-2 total by default; -Cores N to override). Methods:
#     cold    = plain scan
#     swarm   = --swarm <state> (state dir defaults to the Release Licium dir)
#     threads = --seek threads
#     swarmthreads = both flags (growth bias + filament ranking)
#
# Windows PowerShell 5.1.

param(
    [Parameter(Mandatory=$true)][uint32]$Start,
    [Parameter(Mandatory=$true)][uint32]$Budget,
    [int]$Emit = 40,
    [string]$Mix = "cold=100",
    [int]$Cores = 0,
    [string]$OutDir = "",
    [string]$StateDir = ""
)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe  = Join-Path $here "latent_prospect\latent_prospect.exe"
if (-not (Test-Path $exe)) { throw "latent_prospect.exe not found: $exe" }
if ($OutDir -eq "")  { $OutDir  = Join-Path (Split-Path -Parent $here) "vis_milk2\Release\Licium\presets\latent" }
if ($StateDir -eq "") { $StateDir = Join-Path (Split-Path -Parent $here) "vis_milk2\Release\Licium" }
if ($Cores -le 0) { $Cores = [Environment]::ProcessorCount - 2; if ($Cores -lt 1) { $Cores = 1 } }

# parse the mix
$alloc = @()
$totalPct = 0
foreach ($part in $Mix.Split(",")) {
    $kv = $part.Trim().Split("=")
    if ($kv.Count -ne 2) { throw "bad mix entry: $part" }
    $m = $kv[0].ToLower(); $p = [int]$kv[1]
    if ($m -notin @("cold","swarm","threads","swarmthreads")) { throw "unknown method: $m (cold|swarm|threads|swarmthreads)" }
    if ($p -le 0) { continue }
    $alloc += ,@($m, $p)
    $totalPct += $p
}
if ($totalPct -ne 100) { throw "mix must total 100 (got $totalPct)" }

# carve the budget + cores; every slice gets a DISJOINT range
$jobs = @()
$cursor = $Start
for ($i = 0; $i -lt $alloc.Count; $i++) {
    $m = $alloc[$i][0]; $p = $alloc[$i][1]
    $slice  = [uint32][math]::Floor($Budget * $p / 100)
    if ($i -eq $alloc.Count - 1) { $slice = $Start + $Budget - $cursor }   # remainder
    $cores  = [math]::Max(1, [math]::Floor($Cores * $p / 100))
    $emit   = [math]::Max(1, [math]::Floor($Emit * $p / 100))
    $args = "--start $cursor --count $slice --emit $emit --threads $cores --outdir `"$OutDir`""
    if ($m -eq "swarm" -or $m -eq "swarmthreads")   { $args += " --swarm `"$StateDir`"" }
    if ($m -eq "threads" -or $m -eq "swarmthreads") { $args += " --seek threads" }
    $log = Join-Path $env:TEMP ("expedition_" + $m + "_" + $cursor + ".log")
    Write-Host ("{0,-12} seeds [{1}..{2})  cores={3}  emit={4}" -f $m, $cursor, ($cursor + $slice), $cores, $emit)
    $proc = Start-Process -FilePath $exe -ArgumentList $args -NoNewWindow -PassThru `
            -RedirectStandardOutput $log
    $jobs += ,@($m, $proc, $log)
    $cursor += $slice
}

foreach ($j in $jobs) { $j[1].WaitForExit() }
Write-Host "`n== expedition report =="
foreach ($j in $jobs) {
    $last = Get-Content $j[2] -ErrorAction SilentlyContinue | Where-Object { $_ -match "^scan \[" } | Select-Object -Last 1
    Write-Host ("{0,-12} {1}" -f $j[0], $last)
}
Write-Host "rolls -> $OutDir  (run latent_triage.ps1 next)"
