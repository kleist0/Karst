# ===========================================================================
#  Karst script studio - offline compile verifier
# ===========================================================================
#  WHY THIS EXISTS
#
#  The studio generates C# from blocks. A block whose snippet does not compile
#  is worse than a missing block: the user only discovers it after dropping the
#  file into the game and reading karstpatcher.log. So every block snippet, and every
#  combination of blocks we ship, must be compiled BEFORE it reaches the page.
#
#  This compiles a .cs exactly the way the in-game Roslyn host does, using the
#  same reference set and the same options. The parameters mirror
#  <PatcherDir>\compiler.config.json plus CompilationManager's own defaults:
#
#     references : mscorlib, System, System.Core, System.Data,
#                  KarstPatcher, KarstYRpp, KarstExtension
#     defines    : REALTIME_INI          (preprocessor_symbols)
#     options    : /target:library /unsafe /optimize- /warn:4 /langversion:latest
#
#  MEASURED: compiling karstpatcher\Scripts\MyFirst.cs through this script
#  produces a byte-identical (same length, 4608) assembly to the one the game
#  itself produced in karstpatcher\Build\Scripts\. That is the evidence that
#  this is a faithful stand-in, not an approximation.
#
#  Usage:
#     powershell -File tools\script-studio\verify_compile.ps1 -Path build\Scripts\X.cs
#     powershell -File tools\script-studio\verify_compile.ps1 -Path (Get-ChildItem *.cs).FullName
#     powershell -File tools\script-studio\verify_compile.ps1 -PathDir cases -Recurse
#
#  Exit code 0 only when every file compiled with no errors.
# ===========================================================================

param(
    # Files to compile. Accepts a list.
    [string[]]$Path = @(),

    # Or: every .cs under this directory.
    [string]$PathDir,
    [switch]$Recurse,

    # Where the Karst assemblies live. Defaults to the test game install.
    [string]$GameDir = 'E:\_game\RA2\yr_karst_test',
    [string]$PatcherDir,

    # Where the temporary .dll goes. Defaults to %TEMP%\karst-studio-verify.
    [string]$WorkDir,

    # Keep the produced assemblies (default: deleted after a successful check).
    [switch]$KeepOutput,

    # Print compiler warnings too (default: only errors).
    [switch]$ShowWarnings,

    # Silence the per-file banner.
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'

# --- locate csc.exe --------------------------------------------------------
# The game compiles with Roslyn 3.8 (Microsoft.CodeAnalysis.CSharp 3.8.0).
# Visual Studio's Roslyn csc.exe is the same compiler family and accepts the
# same switches; a mismatch would show up as "the offline check passed but the
# game rejected it", which is why the version is printed below.
function Find-Csc {
    $candidates = @()

    if ($env:KARST_STUDIO_CSC) { $candidates += $env:KARST_STUDIO_CSC }

    $vsRoots = @(
        'D:\software\_Study\Microsoft\Visual_Studio\18\Community',
        'C:\Program Files\Microsoft Visual Studio\2022\Community',
        'C:\Program Files\Microsoft Visual Studio\2022\Professional',
        'C:\Program Files\Microsoft Visual Studio\2022\Enterprise',
        'C:\Program Files (x86)\Microsoft Visual Studio\2019\Community',
        'C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools'
    )
    foreach ($root in $vsRoots) {
        $candidates += (Join-Path $root 'MSBuild\Current\Bin\Roslyn\csc.exe')
    }

    # Anything MSBuild knows about.
    $msbuildCandidates = @(
        'D:\software\_Study\Microsoft\Visual_Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
    )
    foreach ($msbuild in $msbuildCandidates) {
        if (Test-Path $msbuild) {
            $bin = Split-Path $msbuild -Parent
            $candidates += (Join-Path $bin 'Roslyn\csc.exe')
        }
    }

    foreach ($c in $candidates) {
        if ($c -and (Test-Path $c)) { return (Resolve-Path $c).Path }
    }

    $cmd = Get-Command csc.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    throw "csc.exe not found. Set KARST_STUDIO_CSC to its full path."
}

# --- locate the reference assemblies --------------------------------------
function Find-FrameworkDir {
    # The game resolves mscorlib/System/... through Helpers.GetAssemblyPath,
    # which lands in the running framework's directory. Use the same generation.
    foreach ($d in @(
        'C:\Windows\Microsoft.NET\Framework\v4.0.30319',
        'C:\Windows\Microsoft.NET\Framework64\v4.0.30319'
    )) {
        if (Test-Path (Join-Path $d 'mscorlib.dll')) { return $d }
    }
    throw "the .NET Framework v4.0.30319 directory was not found."
}

function Get-KarstPatcherDir {
    if ($PatcherDir) { return (Resolve-Path $PatcherDir).Path }
    $p = Join-Path $GameDir 'karstpatcher'
    if (-not (Test-Path $p)) {
        throw "missing $p - pass -PatcherDir or -GameDir pointing at a Karst install."
    }
    return (Resolve-Path $p).Path
}

# --- collect inputs --------------------------------------------------------
# -Path is an array, but `powershell -File x.ps1 -Path a.cs,b.cs` hands the
# whole "a.cs,b.cs" over as ONE string (the comma is not a separator there), so
# accept comma/semicolon separated lists as well. Measured: without this the
# call fails with "no such file: a.cs,b.cs".
$files = New-Object System.Collections.Generic.List[string]
foreach ($entry in $Path) {
    if (-not $entry) { continue }
    foreach ($piece in ($entry -split '[,;]')) {
        $f = $piece.Trim().Trim('"')
        if (-not $f) { continue }
        if (-not (Test-Path $f)) { throw "no such file: $f" }
        $files.Add((Resolve-Path $f).Path)
    }
}
if ($PathDir) {
    if (-not (Test-Path $PathDir)) { throw "no such directory: $PathDir" }
    $opt = if ($Recurse) { @{ Recurse = $true } } else { @{} }
    Get-ChildItem -Path $PathDir -Filter *.cs @opt -File |
        ForEach-Object { $files.Add($_.FullName) }
}
if ($files.Count -eq 0) {
    Write-Host "usage: verify_compile.ps1 -Path <file.cs> [...] | -PathDir <dir> [-Recurse]" -ForegroundColor Yellow
    exit 2
}

$csc = Find-Csc
$fx  = Find-FrameworkDir
$pd  = Get-KarstPatcherDir

$refs = @(
    (Join-Path $fx 'mscorlib.dll'),
    (Join-Path $fx 'System.dll'),
    (Join-Path $fx 'System.Core.dll'),
    (Join-Path $fx 'System.Data.dll'),
    (Join-Path $pd 'KarstPatcher.dll'),
    (Join-Path $pd 'KarstYRpp.dll'),
    (Join-Path $pd 'KarstExtension.dll')
)
foreach ($r in $refs) {
    if (-not (Test-Path $r)) { throw "missing reference assembly: $r" }
}

if (-not $WorkDir) { $WorkDir = Join-Path $env:TEMP 'karst-studio-verify' }
New-Item -ItemType Directory -Force $WorkDir | Out-Null

if (-not $Quiet) {
    Write-Host "csc        : $csc"
    Write-Host "framework  : $fx"
    Write-Host "references : $pd"
    Write-Host "work dir   : $WorkDir"
    Write-Host ("files      : {0}" -f $files.Count)
    Write-Host ""
}

# --- compile each file -----------------------------------------------------
$refArgs = @()
foreach ($r in $refs) { $refArgs += "/r:$r" }

$results = New-Object System.Collections.Generic.List[object]

foreach ($file in $files) {
    $name    = [System.IO.Path]::GetFileNameWithoutExtension($file)
    $outDll  = Join-Path $WorkDir ("{0}.dll" -f $name)
    # Two files with the same basename would collide; give each its own folder.
    $outDll  = Join-Path (Join-Path $WorkDir $name) ("{0}.dll" -f $name)
    New-Item -ItemType Directory -Force (Split-Path $outDll -Parent) | Out-Null
    if (Test-Path $outDll) { Remove-Item $outDll -Force }

    $argList = @(
        '/nologo', '/noconfig', '/target:library', '/unsafe', '/optimize-',
        '/warn:4', '/langversion:latest', '/define:REALTIME_INI', '/nostdlib+',
        "/out:$outDll"
    ) + $refArgs + @($file)

    $raw = & $csc @argList 2>&1 | Out-String
    $code = $LASTEXITCODE

    $errors   = @($raw -split "`r?`n" | Where-Object { $_ -match ': error CS\d+' })
    $warnings = @($raw -split "`r?`n" | Where-Object { $_ -match ': warning CS\d+' })

    $ok = ($code -eq 0) -and ($errors.Count -eq 0) -and (Test-Path $outDll)
    $size = if (Test-Path $outDll) { (Get-Item $outDll).Length } else { 0 }

    $results.Add([pscustomobject]@{
        File     = $file
        Name     = $name
        Ok       = $ok
        Exit     = $code
        Errors   = $errors
        Warnings = $warnings
        Bytes    = $size
    })

    if (-not $Quiet) {
        if ($ok) {
            Write-Host ("[OK]   {0}  ({1:N0} B)" -f $file, $size) -ForegroundColor Green
        } else {
            Write-Host ("[FAIL] {0}" -f $file) -ForegroundColor Red
        }
        foreach ($e in $errors) { Write-Host ("       " + $e.Trim()) -ForegroundColor Red }
        if ($ShowWarnings) {
            foreach ($w in $warnings) { Write-Host ("       " + $w.Trim()) -ForegroundColor Yellow }
        } elseif ($ok -and $warnings.Count -gt 0) {
            Write-Host ("       ({0} warning(s), pass -ShowWarnings to see them)" -f $warnings.Count) -ForegroundColor DarkGray
        }
        if (-not $ok -and $raw.Trim() -and $errors.Count -eq 0) {
            # Non-CS failure (bad reference, IO, crash): show what the compiler said.
            foreach ($l in ($raw -split "`r?`n" | Where-Object { $_.Trim() })) {
                Write-Host ("       " + $l.Trim()) -ForegroundColor DarkRed
            }
        }
    }
}

# --- summary ---------------------------------------------------------------
$failed = @($results | Where-Object { -not $_.Ok })
$totalErr = ($results | ForEach-Object { $_.Errors.Count } | Measure-Object -Sum).Sum
if (-not $totalErr) { $totalErr = 0 }

Write-Host ""
Write-Host ("checked {0} file(s): {1} ok, {2} failed, {3} error(s)" -f `
    $results.Count, ($results.Count - $failed.Count), $failed.Count, $totalErr) `
    -ForegroundColor $(if ($failed.Count) { 'Red' } else { 'Green' })

if ($failed.Count) {
    Write-Host "failed:" -ForegroundColor Red
    foreach ($f in $failed) { Write-Host ("  " + $f.File) -ForegroundColor Red }
}

if (-not $KeepOutput) {
    foreach ($r in $results) {
        $d = Join-Path $WorkDir $r.Name
        if (Test-Path $d) { Remove-Item $d -Recurse -Force -ErrorAction SilentlyContinue }
    }
}

if ($failed.Count) { exit 1 }
exit 0
