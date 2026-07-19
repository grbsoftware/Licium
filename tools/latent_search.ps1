# latent_search.ps1 — the whole search in one command: scan -> triage -> X.
#
# Chains the pipeline that used to be hand-run step by step:
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
    [switch]$NoRefresh       # leave the X browser alone at the end
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
    $proc = Get-Process -Name "Licium" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($proc) {
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
        $main = [LSearch.W]::Find([uint32]$proc.Id, "Licium")
        if ($main -ne [IntPtr]::Zero) {
            # Close-then-open: the browser builds its list at open, so this is
            # how a running instance learns about the new rolls. If X was
            # closed, the close is a no-op and the open just presents the haul.
            [LSearch.W]::PostMessageW($main, 0x8053, [IntPtr]0, [IntPtr]0) | Out-Null
            Start-Sleep -Milliseconds 400
            [LSearch.W]::PostMessageW($main, 0x8053, [IntPtr]1, [IntPtr]0) | Out-Null
            Write-Host "PHASE C: X browser refreshed - the haul is on screen"
        }
    } else {
        Write-Host "PHASE C skipped: Licium not running (triage should have left it up?)"
    }
}

Write-Host "search complete: [$Start, $End) -> $Emit emitted -> triaged -> X"
