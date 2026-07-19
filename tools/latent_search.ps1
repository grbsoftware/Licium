# latent_search.ps1 — the whole search in one command: scan -> triage -> X.
#
# Chains the pipeline that used to be hand-run step by step:
#   Phase 0: HARVEST — queue.jsonl archives every passer forever but each scan
#            only emits its top-K, so good candidates pile up unemitted. Emit
#            the best leftovers, triage them, refresh X: an early haul to
#            browse while Phase A grinds. (Gary's ask, 2026-07-19.)
#   Phase A: latent_prospect.exe scans [Start, Start+Count), emits top rolls
#   Phase B: latent_triage.ps1 verdicts every untriaged roll (incl. these)
#   Phase C: the X browser is (re)opened in the running Licium so the fresh
#            haul is sitting there, unvoted-first, when Gary looks up.
#
# Usage:
#   .\latent_search.ps1 -Start 14000000                      # 1M seeds, emit 40
#   .\latent_search.ps1 -Start 14000000 -Count 5000000 -Emit 60
#   .\latent_search.ps1 -Start 14000000 -Swarm               # swarm mode (LXS1)
#   .\latent_search.ps1 -Start 14000000 -Seek threads        # rank-side profile
#   .\latent_search.ps1 -Start 14000000 -NoRefresh           # don't touch X
#   .\latent_search.ps1 -Start 14000000 -NoHarvest           # skip Phase 0
#     (labeled experiments want hauls that are exactly one range — harvest
#      would mix old-range leftovers into the group being measured)
#
# GOTCHA (from SESSION_STATE): pick a Start outside used seed ranges or the
# queue gets duplicate lines. Used so far: [0, 13.6M) and [20M, 20.03M) tests.
# This script appends the range it consumed to Licium\latent_search_ranges.txt
# so future runs can check.
#
# Windows PowerShell 5.1. Launch detached (WMI) for long scans — the harness
# kills child processes of a returned tool call.

param(
    [Parameter(Mandatory = $true)][long]$Start,
    [long]$Count = 1000000,
    [int]$Emit = 40,
    [int]$Threads = 10,
    [switch]$Swarm,          # generation-side ACO state from Licium\ (LXS1)
    [string]$Seek = "",      # rank-side profile, e.g. "threads"
    [switch]$NoRefresh,      # leave the X browser alone at the end
    [switch]$NoHarvest       # skip Phase 0 (queue-leftover early haul)
)

$ErrorActionPreference = "Stop"

# ---- places (same layout law as latent_triage.ps1) --------------------------
$Repo       = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$ReleaseDir = Join-Path $Repo "vis_milk2\Release"
$LiciumDir  = Join-Path $ReleaseDir "Licium"
$RollsDir   = Join-Path $LiciumDir "presets\latent"
$Prospector = Join-Path $Repo "tools\latent_prospect\latent_prospect.exe"
$TriageScript = Join-Path $Repo "tools\latent_triage.ps1"
$RangesLog  = Join-Path $LiciumDir "latent_search_ranges.txt"

if (-not (Test-Path $Prospector))   { throw "prospector not built: $Prospector (run tools\latent_prospect\build.ps1)" }
if (-not (Test-Path $TriageScript)) { throw "triage script missing: $TriageScript" }

# ---- X refresh (shared by Phase 0 and Phase C) ------------------------------
# Close-then-open: the browser builds its list at open, so this is how a
# running instance learns about new rolls. If X was closed, the close is a
# no-op and the open just presents the haul. Returns $false if Licium is down.
function Refresh-X {
    $proc = Get-Process -Name "Licium" -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $proc) { return $false }
    if (-not ([System.Management.Automation.PSTypeName]'LSearch.W').Type) {
        Add-Type -Name W -Namespace LSearch -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
public delegate bool EnumProc(IntPtr h, IntPtr l);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, System.Text.StringBuilder s, int n);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
[DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
public static IntPtr Find(uint pid, string cls) {
    IntPtr found = IntPtr.Zero;
    EnumWindows((h, l) => {
        uint p; GetWindowThreadProcessId(h, out p);
        if (p != pid) return true;
        var sb = new System.Text.StringBuilder(64);
        GetClassNameW(h, sb, 64);
        if (sb.ToString() == cls) { found = h; return false; }
        return true;
    }, IntPtr.Zero);
    return found;
}
'@
    }
    $main = [LSearch.W]::Find([uint32]$proc.Id, "Licium")
    if ($main -eq [IntPtr]::Zero) { return $false }
    [LSearch.W]::PostMessageW($main, 0x8053, [IntPtr]0, [IntPtr]0) | Out-Null
    Start-Sleep -Milliseconds 400
    [LSearch.W]::PostMessageW($main, 0x8053, [IntPtr]1, [IntPtr]0) | Out-Null
    return $true
}

# ---- range-overlap check (best effort, from this script's own log) ----------
$End = $Start + $Count
if (Test-Path $RangesLog) {
    foreach ($line in Get-Content $RangesLog) {
        if ($line -match '^\s*(\d+)\s+(\d+)') {
            $a = [long]$Matches[1]; $b = [long]$Matches[2]
            if ($Start -lt $b -and $End -gt $a) {
                throw "range [$Start,$End) overlaps already-searched [$a,$b) (latent_search_ranges.txt). Pick a fresh Start."
            }
        }
    }
}

# ---- Phase 0: harvest queue leftovers ---------------------------------------
# Unemitted = "emitted":0 in the queue line AND no latent_<id>.milk on disk
# (the queue is append-only, so a leftover harvested by a PREVIOUS run still
# says emitted:0 — the file check is what keeps this idempotent). Cold current
# lineage only: LXS1 needs its pheromone table to regrow and older genvers are
# superseded; --seed regrows a cold id byte-identically from the seed alone.
if (-not $NoHarvest) {
    $QueuePath = Join-Path $RollsDir "queue.jsonl"
    $lineage = $null
    if (Test-Path $QueuePath) {
        $banner = (& $Prospector) | Select-Object -First 1
        if ("$banner" -match 'latent_prospect \((LX\d+)\)') { $lineage = $Matches[1] }
    }
    if ($lineage) {
        Write-Host "HARVEST: checking queue for unemitted $lineage passers"
        # The queue can hold 100Ks of leftovers: no per-line cmdlets. Existing
        # rolls become a HashSet up front; the List's own Sort (Tuple compares
        # by Item1 = score) replaces Sort-Object.
        $have = New-Object 'System.Collections.Generic.HashSet[string]'
        foreach ($f in [System.IO.Directory]::GetFiles($RollsDir, "latent_*.milk")) {
            [void]$have.Add([System.IO.Path]::GetFileName($f))
        }
        $rx = New-Object System.Text.RegularExpressions.Regex(
            ('"id":"(' + $lineage + '-([0-9A-F]{8}))","score":([0-9.]+).*"emitted":0'), 'Compiled')
        $cand = New-Object 'System.Collections.Generic.List[System.Tuple[double,string,string]]'
        foreach ($line in [System.IO.File]::ReadLines($QueuePath)) {
            $m = $rx.Match($line)
            if ($m.Success -and -not $have.Contains("latent_" + $m.Groups[1].Value + ".milk")) {
                $cand.Add([System.Tuple]::Create([double]$m.Groups[3].Value, $m.Groups[1].Value, $m.Groups[2].Value))
            }
        }
        if ($cand.Count -gt 0) {
            $take = [Math]::Min($Emit, $cand.Count)
            Write-Host "HARVEST: top $take of $($cand.Count) unemitted $lineage queue passers"
            $cand.Sort()   # ascending by score
            $picked = @(); for ($i = $cand.Count - 1; $i -ge $cand.Count - $take; $i--) { $picked += $cand[$i] }
            foreach ($c in $picked) {
                & $Prospector --seed $c.Item3 --outdir $RollsDir | Out-Null
                if ($LASTEXITCODE -ne 0) { throw "harvest emit failed for $($c.Item2)" }
                Write-Host ("  harvested {0} score={1:0.00000}" -f $c.Item2, $c.Item1)
            }
            & $TriageScript -KeepOpen
            if (Refresh-X) { Write-Host "HARVEST done: early haul in X ($take rolls) - scanning next" }
            else           { Write-Host "HARVEST done: $take rolls triaged (Licium not up; X will show them next open)" }
        } else {
            Write-Host "HARVEST: queue has no unemitted $lineage passers"
        }
    }
}

# ---- Phase A: scan + emit ---------------------------------------------------
$argsList = @("--start", $Start, "--count", $Count, "--emit", $Emit,
              "--threads", $Threads, "--outdir", $RollsDir)
if ($Swarm)     { $argsList += @("--swarm", $LiciumDir) }
if ($Seek -ne ""){ $argsList += @("--seek", $Seek) }

Write-Host "PHASE A: scan [$Start, $End) emit $Emit ($(if($Swarm){'swarm'}elseif($Seek){"seek $Seek"}else{'cold'}), $Threads threads)"
& $Prospector @argsList
if ($LASTEXITCODE -ne 0) { throw "prospector failed ($LASTEXITCODE)" }
Add-Content -Path $RangesLog -Value "$Start $End $(Get-Date -Format s)" -Encoding ascii

# ---- Phase B: triage everything untriaged -----------------------------------
Write-Host "PHASE B: triage"
& $TriageScript -KeepOpen
# latent_triage.ps1 throws on its own failures (ErrorActionPreference Stop);
# reaching here means verdicts + contact sheet are written and Licium is up.

# ---- Phase C: refresh the X browser so the haul is on screen ----------------
if (-not $NoRefresh) {
    if (Refresh-X) { Write-Host "PHASE C: X browser refreshed - the haul is on screen" }
    else           { Write-Host "PHASE C skipped: Licium not running (triage should have left it up?)" }
}

Write-Host "search complete: [$Start, $End) -> $Emit emitted -> triaged -> X"
