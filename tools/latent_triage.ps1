# latent_triage.ps1 — Latent Phase B: the automated eye (docs\LATENT_DESIGN.md).
#
# Loads each untriaged latent roll in the REAL renderer via the WM_COPYDATA
# hook (licium_latent.h), Spout-grabs it, and verdicts:
#   black | frozen | blowout | alive | latebloom
# Quick-look failures get a 40s-settle second look (the late-bloomer lesson);
# alive-after-development = latebloom, treated as alive downstream. Verdicts
# APPEND to Licium\latent_verdicts.jsonl — labels, never deletions. A stamped
# contact sheet of the run's grabs lands next to the verdicts.
#
# Manual trigger only in v1. Windows PowerShell 5.1.
#
# Usage:
#   .\latent_triage.ps1                # triage every roll without a verdict
#   .\latent_triage.ps1 -Limit 10      # first N untriaged only
#   .\latent_triage.ps1 -KeepOpen      # leave Licium running afterwards

param(
    [int]$Limit = 0,
    [switch]$KeepOpen,
    # re-triage ids matching this regex even if they already have verdicts
    # (verdict lines are later-wins, so the new line simply outranks the old —
    # added after the 2026-07-18 compile-stall poisoned 48 verdicts)
    [string]$Redo = ""
)

$ErrorActionPreference = "Stop"

# ---- places (repo layout per docs\ + memory: env\ is in the PARENT dir) -----
$Repo      = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$ReleaseDir= Join-Path $Repo "vis_milk2\Release"
$Exe       = Join-Path $ReleaseDir "Licium.exe"
$LiciumDir = Join-Path $ReleaseDir "Licium"
$RollsDir  = Join-Path $LiciumDir "presets\latent"
$Verdicts  = Join-Path $LiciumDir "latent_verdicts.jsonl"
$QuarantDir= Join-Path $RollsDir "quarantine"
$EnvDir    = Join-Path (Split-Path -Parent $Repo) "env"
$Grabber   = Join-Path $EnvDir "spout_grab.exe"
$OutDir    = Join-Path $EnvDir "out\latent_triage"

if (-not (Test-Path $Exe))     { throw "Licium.exe not found: $Exe" }
if (-not (Test-Path $Grabber)) { throw "spout_grab.exe not found: $Grabber" }
New-Item -ItemType Directory -Force $OutDir | Out-Null

# ---- win32 helpers ----------------------------------------------------------
Add-Type -Name W -Namespace LT -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
public delegate bool EnumProc(IntPtr h, IntPtr l);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, System.Text.StringBuilder s, int n);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
[DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
[DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
[DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
[StructLayout(LayoutKind.Sequential)] public struct COPYDATASTRUCT { public IntPtr dwData; public int cbData; public IntPtr lpData; }
[DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, ref COPYDATASTRUCT l);
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
public static int LoadPreset(IntPtr hwnd, string path) {
    var bytes = System.Text.Encoding.Unicode.GetBytes(path + "\0");
    var mem = Marshal.AllocHGlobal(bytes.Length);
    Marshal.Copy(bytes, 0, mem, bytes.Length);
    var cd = new COPYDATASTRUCT { dwData = (IntPtr)0x4C4F4144, cbData = bytes.Length, lpData = mem };
    var r = SendMessageW(hwnd, 0x004A, IntPtr.Zero, ref cd);   // WM_COPYDATA
    Marshal.FreeHGlobal(mem);
    return (int)r;
}
'@

Add-Type -AssemblyName System.Drawing

# ---- launch Licium detached (WMI — a child process would die with the shell) -
function Start-Licium {
    $existing = Get-Process -Name "Licium" -ErrorAction SilentlyContinue
    if ($existing) { return $existing.Id }
    $r = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{
        CommandLine = "`"$Exe`""; CurrentDirectory = $ReleaseDir }
    if ($r.ReturnValue -ne 0) { throw "WMI launch failed rc=$($r.ReturnValue) (rc=9 after a build: suspect AV quarantine)" }
    Start-Sleep -Seconds 4
    # The splash holds forever until clicked — and swallows posted messages.
    $splash = [LT.W]::Find([uint32]$r.ProcessId, "LiciumSplash")
    if ($splash -ne [IntPtr]::Zero) {
        [LT.W]::PostMessageW($splash, 0x0201, [IntPtr]1, [IntPtr]0) | Out-Null  # WM_LBUTTONDOWN
        [LT.W]::PostMessageW($splash, 0x0202, [IntPtr]0, [IntPtr]0) | Out-Null  # WM_LBUTTONUP
        Start-Sleep -Milliseconds 800
    }
    return $r.ProcessId
}

# A roll can take the whole process down INSIDE the load call: the legacy
# d3dx9 compiler crashes (not errors) on some evolved shader expressions, and
# the load is synchronous, so SendMessage just returns 0. Without this check
# every remaining roll logs a loadfail against a corpse — that cascade poisoned
# the verdicts file three times before the guard existed.
function Test-EngineAlive([uint32]$ProcId) {
    return [bool](Get-Process -Id $ProcId -ErrorAction SilentlyContinue)
}

function Restart-Engine {
    Get-Process -Name "Licium" -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2
    $p = Start-Licium
    $h = Get-MainWindow ([uint32]$p)
    if ([LT.W]::IsIconic($h)) {
        [LT.W]::PostMessageW($h, 0x0112, [IntPtr]0xF120, [IntPtr]0) | Out-Null   # SC_RESTORE
        Start-Sleep -Seconds 2
    }
    return @{ ProcId = $p; Main = $h }
}

function Get-MainWindow([uint32]$ProcId) {
    for ($i = 0; $i -lt 20; $i++) {
        $h = [LT.W]::Find($ProcId, "Licium")
        if ($h -ne [IntPtr]::Zero) { return $h }
        Start-Sleep -Milliseconds 500
    }
    throw "Licium main window not found (pid $ProcId)"
}

# ---- grab + measure ---------------------------------------------------------
function Grab-Frames([string]$Prefix, [int]$Frames, [int]$IntervalMs) {
    & $Grabber --sender Licium --frames $Frames --interval $IntervalMs --prefix $Prefix --out $OutDir | Out-Null
    return (Get-ChildItem "$OutDir\$Prefix*.png" | Sort-Object Name)
}

function Measure-Png([string]$Path) {
    $bmp = [System.Drawing.Bitmap]::new($Path)
    try {
        # 48x48 sample lattice: mean luminance, bright fraction, per-pixel list
        $lums = New-Object System.Collections.Generic.List[double]
        $bright = 0; $n = 0
        for ($y = 0; $y -lt 48; $y++) {
            for ($x = 0; $x -lt 48; $x++) {
                $px = $bmp.GetPixel([int](($x + 0.5) * $bmp.Width / 48), [int](($y + 0.5) * $bmp.Height / 48))
                $l = (0.32 * $px.R + 0.49 * $px.G + 0.29 * $px.B) / 255.0
                $lums.Add($l); $n++
                if ($l -gt 0.92) { $bright++ }
            }
        }
        $mean = ($lums | Measure-Object -Average).Average
        return @{ Mean = $mean; BrightFrac = $bright / $n; Lums = $lums }
    } finally { $bmp.Dispose() }
}

function Diff-Lums($A, $B) {
    $d = 0.0
    for ($i = 0; $i -lt $A.Count; $i++) { $d += [math]::Abs($A[$i] - $B[$i]) }
    return $d / $A.Count
}

# Quick look at one loaded roll: settle, two grabs 1.5s apart.
function Judge([string]$Prefix) {
    $pngs = Grab-Frames $Prefix 2 1500
    if ($pngs.Count -lt 2) { return @{ Verdict = "grabfail"; Mean = 0; Motion = 0 } }
    $a = Measure-Png $pngs[0].FullName
    $b = Measure-Png $pngs[1].FullName
    $motion = Diff-Lums $a.Lums $b.Lums
    if ($b.Mean -lt 0.015)                        { return @{ Verdict = "black";   Mean = $b.Mean; Motion = $motion } }
    if ($b.BrightFrac -gt 0.85)                   { return @{ Verdict = "blowout"; Mean = $b.Mean; Motion = $motion } }
    # 0.008, not ~0: the comp's time-animated sheen light drifts even over a
    # DEAD field, so a flat plate still shows ~0.002-0.004 "motion". Real alive
    # rolls measured >=0.02 in the first calibration run (2026-07-17).
    if ($motion -lt 0.008)                        { return @{ Verdict = "frozen";  Mean = $b.Mean; Motion = $motion } }
    return @{ Verdict = "alive"; Mean = $b.Mean; Motion = $motion }
}

# ---- main -------------------------------------------------------------------
$already = @{}
if (Test-Path $Verdicts) {
    Get-Content $Verdicts | ForEach-Object {
        try { $j = $_ | ConvertFrom-Json; $already[$j.id] = $j.verdict } catch {}
    }
}

# LX\w+ not LX\d+: lineages may carry letters (LXS1 swarm rolls).
# -Redo: ids matching the regex re-triage even when already verdicted — the
# new line simply outranks the old (later-wins), no manual line surgery.
$rolls = @(Get-ChildItem "$RollsDir\latent_LX*.milk" -ErrorAction SilentlyContinue | Sort-Object Name |
    Where-Object { $_.BaseName -match 'latent_(LX\w+-[0-9A-F]{8})' -and
        ((-not $already.ContainsKey($Matches[1])) -or ($Redo -ne "" -and $Matches[1] -match $Redo)) })
if ($Limit -gt 0 -and $rolls.Count -gt $Limit) { $rolls = $rolls[0..($Limit - 1)] }
if ($rolls.Count -eq 0) { Write-Host "nothing untriaged in $RollsDir"; return }
Write-Host "triage: $($rolls.Count) untriaged roll(s)"

$procId = Start-Licium
$main = Get-MainWindow ([uint32]$procId)

# A minimized window stops presenting, so Spout re-serves the last frame and
# every grab is identical — restore before judging anything.
if ([LT.W]::IsIconic($main)) {
    [LT.W]::PostMessageW($main, 0x0112, [IntPtr]0xF120, [IntPtr]0) | Out-Null  # WM_SYSCOMMAND SC_RESTORE
    Start-Sleep -Seconds 2
}

$results = @()
$streakMean = -1.0; $streak = 0
foreach ($roll in $rolls) {
    $null = $roll.BaseName -match 'latent_(LX\w+-[0-9A-F]{8})'
    $id = $Matches[1]
    # Purge this id's grabs from any earlier run: the sheet and any human
    # eyeball must only ever see THIS run's frames (leftover _s files from an
    # aborted run once made 44 healthy verdicts look like identical clones).
    Get-ChildItem "$OutDir\$id`_*.png" -ErrorAction SilentlyContinue | Remove-Item -Force -Confirm:$false
    $rc = [LT.W]::LoadPreset($main, $roll.FullName)
    $crashed = ($rc -ne 1) -and -not (Test-EngineAlive ([uint32]$procId))
    if ($crashed) {
        # Engine died on this roll. Bring it back and give the roll exactly one
        # more chance — a second death is deterministic, so quarantine it and
        # keep going instead of losing the rest of the run to it.
        Write-Host ("  {0}  engine died on load - relaunching for a retry" -f $id)
        $e = Restart-Engine; $procId = $e.ProcId; $main = $e.Main
        $rc = [LT.W]::LoadPreset($main, $roll.FullName)
        $crashed = ($rc -ne 1) -and -not (Test-EngineAlive ([uint32]$procId))
        if ($crashed) {
            $e = Restart-Engine; $procId = $e.ProcId; $main = $e.Main
            New-Item -ItemType Directory -Force $QuarantDir | Out-Null
            Move-Item $roll.FullName (Join-Path $QuarantDir $roll.Name) -Force
        }
    }
    if ($crashed) {
        $res = @{ Verdict = "crasher"; Mean = 0; Motion = 0 }
    } elseif ($rc -ne 1) {
        $res = @{ Verdict = "loadfail"; Mean = 0; Motion = 0 }
    } else {
        Start-Sleep -Seconds 4                  # first development
        $res = Judge "$id`_q"
        if ($res.Verdict -in @("black", "frozen")) {
            # The late-bloomer lesson: give quick-look failures a settled look.
            Start-Sleep -Seconds 40
            $second = Judge "$id`_s"
            if ($second.Verdict -eq "alive") {
                $second.Verdict = "latebloom"
                $res = $second
            }
        }
    }
    $line = '{"id":"' + $id + '","verdict":"' + $res.Verdict + '","mean":' +
        ('{0:0.0000}' -f [double]$res.Mean) + ',"motion":' + ('{0:0.00000}' -f [double]$res.Motion) +
        ',"when":"' + (Get-Date -Format s) + '"}'
    Add-Content -Path $Verdicts -Value $line -Encoding ascii
    Write-Host ("  {0}  {1}  mean={2:0.000} motion={3:0.0000}" -f $id, $res.Verdict, $res.Mean, $res.Motion)
    $results += @{ Id = $id; Verdict = $res.Verdict }
    # Distinct rolls cannot agree on mean to 4 decimals with zero motion — a
    # streak of them means the grabber is photographing one stale frame
    # (minimized window, held splash, blocked message pump...). Abort loudly
    # rather than mass-produce false verdicts; the streak's lines need manual
    # removal from the verdicts file before a re-run will revisit those ids.
    # Only grab-derived verdicts can be stale-frame artifacts. crasher/loadfail
    # are decided without a grab, so they must not feed the streak counter —
    # engine death has its own handling above.
    if ($res.Verdict -in @("crasher", "loadfail")) { $streakMean = -1.0; $streak = 0 }
    elseif (('{0:0.0000}' -f [double]$res.Mean) -eq ('{0:0.0000}' -f $streakMean) -and [double]$res.Motion -lt 0.0005) {
        $streak++
        if ($streak -ge 4) { throw "STALE-FRAME GUARD: $($streak+1) consecutive identical verdicts (mean=$('{0:0.0000}' -f $streakMean), motion~0). Renderer is not presenting. Last ids: $(($results | Select-Object -Last ($streak+1) | ForEach-Object { $_.Id }) -join ', ')" }
    } else { $streakMean = [double]$res.Mean; $streak = 0 }
}

# ---- contact sheet (stamped: id + verdict under each tile) ------------------
if ($results.Count -gt 0) {
    $tileW = 320; $tileH = 200; $label = 26
    $cols = [math]::Min(6, $results.Count)
    $rows = [math]::Ceiling($results.Count / $cols)
    $sheet = [System.Drawing.Bitmap]::new($cols * $tileW, $rows * ($tileH + $label))
    $g = [System.Drawing.Graphics]::FromImage($sheet)
    $g.Clear([System.Drawing.Color]::Black)
    $font = [System.Drawing.Font]::new("Consolas", 10)
    $white = [System.Drawing.Brushes]::White
    for ($i = 0; $i -lt $results.Count; $i++) {
        $r = $results[$i]
        $x = ($i % $cols) * $tileW; $y = [math]::Floor($i / $cols) * ($tileH + $label)
        $png = @(Get-ChildItem "$OutDir\$($r.Id)_*.png" | Sort-Object LastWriteTime | Select-Object -Last 1)
        if ($png.Count -gt 0) {
            $tile = [System.Drawing.Bitmap]::new($png[0].FullName)
            $g.DrawImage($tile, [System.Drawing.Rectangle]::new($x, $y, $tileW, $tileH))
            $tile.Dispose()
        }
        $g.DrawString("$($r.Id)  $($r.Verdict)", $font, $white, $x + 4, $y + $tileH + 4)
    }
    $g.Dispose()
    $sheetPath = Join-Path $OutDir ("sheet_{0}.png" -f (Get-Date -Format "yyyyMMdd_HHmmss"))
    $sheet.Save($sheetPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $sheet.Dispose()
    Write-Host "contact sheet: $sheetPath"
}

if (-not $KeepOpen) {
    Get-Process -Name "Licium" -ErrorAction SilentlyContinue | Stop-Process -Force
}
$tally = $results | Group-Object { $_.Verdict } | ForEach-Object { "$($_.Name)=$($_.Count)" }
Write-Host "done: $($tally -join ', ')  -> $Verdicts"
