# Karst: build (Release x86) -> validate hooks -> deploy to the test install.
#
# WHY A SCRIPT AND NOT JUST "Build" IN VISUAL STUDIO
#
# Building is the easy half. Two things Visual Studio will not do for you, and
# both have already cost this project a crash:
#
#   * A hook whose declared size does not end on an instruction boundary, or is
#     shorter than the 5-byte jmp, corrupts the game (tools\hookcheck.py).
#   * A hook whose bytes overlap another extension's hook makes both of them
#     resume inside an instruction. Measured: Karst's GetCameo hook at 0x712040
#     declared size 6, Ares hooks the same function at 0x712045 with size 5, and
#     the game died at 0xC0000005 in 0x71204D the moment a construction yard was
#     deployed (tools\hookconflict.py).
#
# The script refuses to deploy when either check fails, and refuses to deploy at
# all while the game is running (the DLL would be locked anyway).
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File Karst\tools\build_deploy.ps1
#   powershell -ExecutionPolicy Bypass -File Karst\tools\build_deploy.ps1 -Rebuild
#   powershell -ExecutionPolicy Bypass -File Karst\tools\build_deploy.ps1 -NoDeploy

param(
    [switch]$Rebuild,     # full rebuild instead of an incremental build
    [switch]$NoDeploy     # build and validate only
)

$ErrorActionPreference = 'Stop'

# --- locations ------------------------------------------------------------
$tools     = $PSScriptRoot
$karst     = Split-Path -Parent $tools
$sln       = Join-Path $karst 'Karst.sln'
$builtDll  = Join-Path $karst 'Release\Karst.dll'

$msbuild   = 'D:\software\_Study\Microsoft\Visual_Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
$python    = 'C:\Users\Lenovo\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe'

# The TEST INSTALL is the only place this script writes to.
#
# It deliberately does NOT touch the mod's own folder under
# `_material\...\yr_enh`: that is a shipping directory, and a build under test
# has no business being copied into it. (The old flow went through a
# `Karst_new.dll` there because restart_test.bat used to copy from it; that
# dependency is gone.)
$testDir   = 'E:\_game\RA2\yr_karst_test'
$deployDll = Join-Path $testDir 'Karst.dll'

$extensions = @('Ares.dll', 'Phobos.dll', 'Kratos.dll') |
    ForEach-Object { Join-Path $testDir $_ } |
    Where-Object { Test-Path $_ }

function Step($text) { Write-Host "`n=== $text" -ForegroundColor Cyan }
function Fail($text) { Write-Host "  $text" -ForegroundColor Red; exit 1 }
function Ok($text)   { Write-Host "  $text" -ForegroundColor Green }

# --- preflight ------------------------------------------------------------
Step 'preflight'
foreach ($path in @($msbuild, $python)) {
    if (-not (Test-Path $path)) { Fail "missing: $path" }
}
if (-not (Test-Path $sln)) { Fail "missing: $sln" }
Ok "msbuild + python found"

$game = Get-Process gamemd -ErrorAction SilentlyContinue
if ($game) {
    Write-Host "  gamemd.exe is running (pid $($game.Id)) - close it before deploying" -ForegroundColor Yellow
}

# --- build ----------------------------------------------------------------
Step "build $(if ($Rebuild) { '(rebuild)' } else { '(incremental)' }) Release|x86"
$target = if ($Rebuild) { 'Rebuild' } else { 'Build' }
& $msbuild $sln "/t:$target" /p:Configuration=Release /p:Platform=x86 /nologo /v:minimal
if ($LASTEXITCODE -ne 0) { Fail "build failed (exit $LASTEXITCODE)" }
if (-not (Test-Path $builtDll)) { Fail "build produced no $builtDll" }
Ok ("{0:N0} bytes @ {1}" -f (Get-Item $builtDll).Length, (Get-Item $builtDll).LastWriteTime)

# --- hook shape -----------------------------------------------------------
Step 'hookcheck (size >= 5, whole instructions)'
& $python (Join-Path $tools 'hookcheck.py') $builtDll
if ($LASTEXITCODE -ne 0) { Fail 'hookcheck reported unsafe hooks - NOT deploying' }
Ok 'all hooks well formed'

# --- hook collisions with the other extensions ----------------------------
if ($extensions.Count -gt 0) {
    Step 'hookconflict (bytes shared with Ares / Phobos / Kratos)'
    & $python (Join-Path $tools 'hookconflict.py') $builtDll @extensions
    if ($LASTEXITCODE -ne 0) { Fail 'hook overlap - NOT deploying' }
    Ok 'no overlapping hooks'
} else {
    Write-Host "`n=== hookconflict skipped (no extension DLLs under $testDir)" -ForegroundColor Yellow
}

# --- deploy ---------------------------------------------------------------
if ($NoDeploy) {
    Step 'deploy skipped (-NoDeploy)'
    exit 0
}

Step 'deploy'
if ($game) {
    Fail "gamemd.exe is still running - close the game and run this again"
}
Copy-Item $builtDll $deployDll -Force
$same = (Get-FileHash $builtDll).Hash -eq (Get-FileHash $deployDll).Hash
Ok ("{0:N0} bytes -> {1} (hash match: {2})" -f (Get-Item $deployDll).Length, $deployDll, $same)
Write-Host "`nNow run $testDir\restart_test.bat and re-enter the game." -ForegroundColor Cyan
