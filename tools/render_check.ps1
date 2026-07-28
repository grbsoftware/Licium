# render_check.ps1 — load a handful of .milk files in the REAL engine and report
# whether each one survives.
#
# Why this exists separately from latent_triage.ps1: triage grades every roll
# WITHOUT a verdict, so it cannot be pointed at a specific file (its -Redo is
# additive to the untriaged set, not a filter). After a genver bump the only
# question that matters is narrower than grading — does the evolved HLSL make it
# through the legacy d3dx9 compiler at all? That compiler CRASHES the process on
# some expressions rather than returning an error, so "the worker is still alive
# after the load" IS the pass condition.
#
# Renders off-screen in --worker mode (no splash, own Spout name), so it never
# touches a foreground Licium the user is browsing in.
#
#   .\render_check.ps1 -Presets "C:\path\a.milk","C:\path\b.milk"
#   .\render_check.ps1 -Presets (Get-ChildItem out\*.milk | % FullName) -Grab

param(
    [Parameter(Mandatory=$true)][string[]]$Presets,
    [switch]$Grab,                 # also Spout-grab a frame per preset
    [int]$SettleMs = 2500
)

$ErrorActionPreference = "Stop"

$Repo       = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$ReleaseDir = Join-Path $Repo "vis_milk2\Release"
$Exe        = Join-Path $ReleaseDir "Licium.exe"
$EnvDir     = Join-Path (Split-Path -Parent $Repo) "env"
$Grabber    = Join-Path $EnvDir "spout_grab.exe"
$OutDir     = Join-Path $EnvDir "out\render_check"

if (-not (Test-Path $Exe)) { throw "Licium.exe not found: $Exe" }
New-Item -ItemType Directory -Force $OutDir | Out-Null

# HARD GUARD - learned the expensive way, 2026-07-28.
# EVERY --worker instance publishes under the SAME Spout sender name
# ('LiciumTriage'), so two of them are indistinguishable to spout_grab. This
# script ran while a deferred latent_triage.ps1 woke up, and the two fought over
# the sender: triage logged 5 healthy LX3 rolls as "grabfail" (mean 0.0000,
# motion 0.00000) and then died. Verdicts are later-wins so nothing was lost
# permanently, but the run was wasted and the file needed a -Redo pass.
# Refusing to start is the only reliable fix while the sender name is shared.
$triage = @(Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" -ErrorAction SilentlyContinue |
            Where-Object { $_.CommandLine -and $_.CommandLine -match 'latent_(triage|search)\.ps1' })
if ($triage.Count -gt 0) {
    throw ("refusing to run: latent_triage/latent_search is active (pid {0}). " -f ($triage[0].ProcessId)) +
          "Both use --worker, which shares the 'LiciumTriage' Spout sender, and concurrent grabs poison each other. " +
          "Wait for it to finish, or stop it first."
}

Add-Type -Name RC -Namespace RChk -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
public delegate bool EnumProc(IntPtr h, IntPtr l);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, System.Text.StringBuilder s, int n);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
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
    var r = SendMessageW(hwnd, 0x004A, IntPtr.Zero, ref cd);
    Marshal.FreeHGlobal(mem);
    return (int)r;
}
'@

function Start-Worker {
    $r = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{
        CommandLine = "`"$Exe`" --worker"; CurrentDirectory = $ReleaseDir }
    if ($r.ReturnValue -ne 0) { throw "WMI launch failed rc=$($r.ReturnValue)" }
    Start-Sleep -Seconds 4
    $pid2 = [uint32]$r.ProcessId
    for ($i = 0; $i -lt 20; $i++) {
        $h = [RChk.RC]::Find($pid2, "Licium")
        if ($h -ne [IntPtr]::Zero) { return @{ Pid = $pid2; Hwnd = $h } }
        Start-Sleep -Milliseconds 500
    }
    throw "worker window never appeared (pid $pid2)"
}

$w = Start-Worker
Write-Host "worker pid $($w.Pid)"
$pass = 0; $fail = 0

foreach ($p in $Presets) {
    $name = Split-Path -Leaf $p
    if (-not (Test-Path $p)) { Write-Host "  MISSING  $name"; $fail++; continue }

    # a crashed worker takes the rest of the run with it unless we respawn
    if (-not (Get-Process -Id $w.Pid -ErrorAction SilentlyContinue)) {
        Write-Host "  (worker died - respawning)"
        $w = Start-Worker
    }

    [void][RChk.RC]::LoadPreset($w.Hwnd, $p)
    Start-Sleep -Milliseconds $SettleMs

    if (Get-Process -Id $w.Pid -ErrorAction SilentlyContinue) {
        $extra = ""
        if ($Grab -and (Test-Path $Grabber)) {
            # spout_grab ignores the path it is handed and writes frame_NNN.png
            # into its OWN env\out; take the newest from there, not from cwd.
            $grabOut = Join-Path $EnvDir "out"
            Get-ChildItem "$grabOut\frame_*.png" -ErrorAction SilentlyContinue | Remove-Item -Force
            & $Grabber "$name.png" --sender LiciumTriage 2>&1 | Out-Null
            $g = Get-ChildItem "$grabOut\frame_*.png" -ErrorAction SilentlyContinue |
                 Sort-Object LastWriteTime | Select-Object -Last 1
            if ($g) {
                $dst = Join-Path $OutDir ("{0}.png" -f $name)
                Move-Item $g.FullName $dst -Force
                Get-ChildItem "$grabOut\frame_*.png" -ErrorAction SilentlyContinue | Remove-Item -Force
                $extra = "  grab $([int]((Get-Item $dst).Length/1024))KB"
            }
        }
        Write-Host "  OK       $name$extra"; $pass++
    }
    else {
        # process gone => the d3dx9 compiler took it down on this preset
        Write-Host "  CRASHED  $name"; $fail++
    }
}

Stop-Process -Id $w.Pid -Force -ErrorAction SilentlyContinue
Write-Host ""
Write-Host "render_check: $pass ok, $fail failed, of $($Presets.Count)"
if ($fail -gt 0) { exit 1 }
