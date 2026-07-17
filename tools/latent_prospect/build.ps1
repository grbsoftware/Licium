# build.ps1 — builds latent_prospect.exe with the same VS2022 BuildTools the
# engine uses (no vcxproj needed for a one-file CLI). Run from anywhere.
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$vsdev = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
if (-not (Test-Path $vsdev)) { throw "VS2022 BuildTools not found at $vsdev" }
# One cmd hop: VsDevCmd sets the env, then cl compiles. x64 is fine — the CLI
# never links against the engine.
cmd /c "`"$vsdev`" -arch=x64 -no_logo && cl /nologo /O2 /EHsc /W3 /D_CRT_SECURE_NO_WARNINGS /Fe:`"$here\latent_prospect.exe`" /Fo:`"$here\latent_prospect.obj`" `"$here\latent_prospect.cpp`""
if ($LASTEXITCODE -ne 0) { throw "cl failed ($LASTEXITCODE)" }
Write-Host "built: $here\latent_prospect.exe"
