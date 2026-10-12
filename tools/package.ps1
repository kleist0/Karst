# ===========================================================================
#  OBSOLETE - use package_full.ps1 instead.
# ===========================================================================
#  WHY THIS FILE STILL EXISTS
#
#  This script packages ONLY Karst.dll. But Karst is two halves: without the
#  whole KarstPatcher\ folder (the C# host + Roslyn + dependencies, ~16 MB) the
#  user's .cs scripts are silently ignored at runtime - no error, no log line,
#  just nothing happening. Measured 2026-10-12: a zip produced by this script
#  held Karst.dll at 112,128 bytes while the current build is 263,168 bytes,
#  i.e. that package predates the script system entirely.
#
#  Rather than delete it (it was the documented way to package for a while, and
#  a silent mis-package is worse than a loud error), it now forwards.
#
#  Usage - identical to package_full.ps1:
#     package.ps1
#     package.ps1 -NoBuild
#     package.ps1 -Deploy "E:\_game\RA2\yr_karst_test_2"
#
#  NOTE: this file is pure ASCII on purpose (no BOM needed); keep it that way or
#  Windows PowerShell 5.1 will mis-parse any Chinese as described in the
#  handover doc.
# ===========================================================================

param(
    [switch]$NoBuild,          # package whatever is already built
    [string]$Deploy = ''       # also copy the result into this game folder
)

$ErrorActionPreference = 'Stop'

Write-Host ""
Write-Host "NOTE: package.ps1 is obsolete - it packaged only Karst.dll, without the" -ForegroundColor Yellow
Write-Host "      C# script system (KarstPatcher\). Forwarding to package_full.ps1." -ForegroundColor Yellow
Write-Host ""

$forward = @{}
if ($NoBuild) { $forward['NoBuild'] = $true }
if ($Deploy)  { $forward['Deploy']  = $Deploy }

& (Join-Path $PSScriptRoot 'package_full.ps1') @forward
exit $LASTEXITCODE