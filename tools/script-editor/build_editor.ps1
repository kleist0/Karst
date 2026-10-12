# ===========================================================================
#  Karst script editor - build a single self-contained HTML page
# ===========================================================================
#  The editor needs to offer the game's own building IDs and ART section names.
#  Rather than ship a stale hard-coded list, this reads them from the game's
#  rules files and bakes them into the page, so the result is one file that
#  opens by double-clicking - no server, no folder permission problems.
#
#  Building IDs come from artmd.ini sections that declare Foundation=, which is
#  what a building has and an animation/projectile does not (measured on this
#  install: 568 entries, and it correctly contains GAPILE/GAPOWR while excluding
#  HIND/ARMOR/AMMOCRAT).
#
#  Usage:
#     powershell -File tools\script-editor\build_editor.ps1
#     powershell -File tools\script-editor\build_editor.ps1 -GameDir "E:\game\RA2\yr_karst_test"
# ===========================================================================

param(
    [string]$GameDir = 'E:\_game\RA2\yr_karst_test_2',
    [string]$OutFile
)

$ErrorActionPreference = 'Stop'

$here = $PSScriptRoot
$repo = Split-Path -Parent (Split-Path -Parent $here)   # Karst\

if (-not $OutFile) { $OutFile = Join-Path $here 'script-editor.html' }

$template = Join-Path $here 'editor.template.html'
if (-not (Test-Path $template)) { throw "missing template: $template" }

$artPath = Join-Path $GameDir 'artmd.ini'
if (-not (Test-Path $artPath)) { throw "missing rules file: $artPath" }

# --- read artmd.ini --------------------------------------------------------
Write-Host "reading $artPath"
$lines = [System.IO.File]::ReadAllLines($artPath)

$sections = New-Object System.Collections.Generic.List[string]
$buildingIds = New-Object System.Collections.Generic.List[string]

$current = $null
$hasFoundation = $false

foreach ($line in $lines) {
    $t = $line.Trim()
    if ($t.Length -eq 0 -or $t[0] -eq ';' -or $t[0] -eq '#') { continue }

    if ($t[0] -eq '[') {
        if ($current -and $hasFoundation) { $buildingIds.Add($current) }
        $close = $t.IndexOf(']')
        $current = if ($close -gt 1) { $t.Substring(1, $close - 1).Trim() } else { $null }
        if ($current) { $sections.Add($current) }
        $hasFoundation = $false
        continue
    }

    if ($current -and -not $hasFoundation) {
        $eq = $t.IndexOf('=')
        if ($eq -gt 0 -and $t.Substring(0, $eq).Trim() -ieq 'Foundation') {
            $hasFoundation = $true
        }
    }
}
if ($current -and $hasFoundation) { $buildingIds.Add($current) }

$buildings = $buildingIds | Sort-Object -Unique
$arts = $sections | Sort-Object -Unique

Write-Host ("  building ids : {0}" -f $buildings.Count)
Write-Host ("  ART sections : {0}" -f $arts.Count)

# --- bake ------------------------------------------------------------------
function To-JsArray([string[]]$values) {
    ($values | ForEach-Object { '"' + ($_ -replace '\\', '\\\\') + '"' }) -join ','
}

$html = [System.IO.File]::ReadAllText($template, [System.Text.Encoding]::UTF8)
$html = $html.Replace('/*__BUILDINGS__*/', (To-JsArray $buildings))
$html = $html.Replace('/*__ARTS__*/', (To-JsArray $arts))

# Stamp where the data came from, so a page built against a mod's rules says so.
$stamp = "数据来源：$GameDir（建筑 $($buildings.Count) 项，ART 段 $($arts.Count) 项）"
$html = $html.Replace('<!--__STAMP__-->', "<!-- $stamp -->")

[System.IO.File]::WriteAllText($OutFile, $html, (New-Object System.Text.UTF8Encoding($false)))

$size = (Get-Item $OutFile).Length
Write-Host ("wrote {0} ({1:N0} bytes)" -f $OutFile, $size)
Write-Host "双击这个文件即可打开编辑器。"
