# ===========================================================================
#  Karst script studio - build the single-file page
# ===========================================================================
#  Produces KarstScriptStudio.html: one file, double-click to open, no server.
#
#  Three sources are merged:
#     1. studio.template.html     the shell (markup, styles, UI wiring)
#     2. blocks\*.js              the block library + code generator
#     3. game data                extracted here from rulesmd.ini / artmd.ini
#
#  WHY THE BLOCK LIBRARY IS A SEPARATE JS FILE
#
#  The library is plain JS with a `module.exports` guard, so the SAME file is
#  used by the page and by tests\build_cases.js under Node. That is what makes
#  "every block compiles" a machine-checkable claim instead of a promise: the
#  test generates one .cs per block (and per realistic combination) through the
#  very same generator the page calls, and compiles them all.
#
#  WHY THE GAME DATA IS BAKED IN
#
#  Leaving it out would turn every weapon/warhead picker into a text box the
#  user has to spell correctly - and a typo there is a silent runtime no-op
#  (`ABSTRACTTYPE_ARRAY.Find` returns null). Baking the real names in means the
#  page can offer them. Measured on this install: see the counters it prints.
#
#  NOTE ON weapons: original YR has no [WeaponTypes] list section - a weapon is
#  simply a rulesmd.ini section, and what makes it a weapon is its Projectile=
#  key. So weapons are collected by section-shape, not from a list.
#
#  Usage:
#     powershell -ExecutionPolicy Bypass -File tools\script-studio\build_studio.ps1
#     powershell -ExecutionPolicy Bypass -File tools\script-studio\build_studio.ps1 -GameDir "E:\game\RA2\yr_karst_test"
# ===========================================================================

param(
    [string]$GameDir = 'E:\_game\RA2\yr_karst_test_2',
    [string]$OutFile
)

$ErrorActionPreference = 'Stop'

$here = $PSScriptRoot
if (-not $OutFile) { $OutFile = Join-Path $here 'KarstScriptStudio.html' }

$template = Join-Path $here 'studio.template.html'
if (-not (Test-Path $template)) { throw "missing template: $template" }

$rulesPath = Join-Path $GameDir 'rulesmd.ini'
$artPath   = Join-Path $GameDir 'artmd.ini'
if (-not (Test-Path $rulesPath)) { throw "missing rules file: $rulesPath" }
if (-not (Test-Path $artPath))   { throw "missing rules file: $artPath" }

# The rules files are ISO-8859-1 (see HANDOFF4: byte-safe INI handling). Read
# them that way; decoding as UTF-8 would throw on a stray high byte.
$latin1 = [System.Text.Encoding]::GetEncoding(28591)

# --- one pass over a rules file -------------------------------------------
# Collects, in a single scan:
#   * values of the list sections we care about
#   * section names that carry a Projectile= key  -> weapon names
#   * Warhead= values                              -> warhead names
#   * Projectile= values                           -> projectile names
function Get-RulesInventory {
    param(
        [string]$Path,
        [hashtable]$ListSections,     # section name (lower) -> collection name
        [hashtable]$Out
    )

    $lines = [System.IO.File]::ReadAllLines($Path, $latin1)

    $cur = ''
    $curIsWeapon = $false
    $inList = ''
    $n = 0

    foreach ($line in $lines) {
        $n++
        $t = $line.Trim()
        if ($t.Length -eq 0) { continue }
        $c0 = $t[0]
        if ($c0 -eq ';' -or $c0 -eq '#') { continue }

        if ($c0 -eq '[') {
            if ($curIsWeapon -and $cur) { [void]$Out.weapons.Add($cur) }
            $close = $t.IndexOf(']')
            if ($close -gt 1) {
                $cur = $t.Substring(1, $close - 1).Trim()
            } else {
                $cur = ''
            }
            $curIsWeapon = $false
            $key = $cur.ToLowerInvariant()
            $inList = if ($ListSections.ContainsKey($key)) { $ListSections[$key] } else { '' }
            continue
        }

        $eq = $t.IndexOf('=')
        if ($eq -le 0) { continue }
        $k = $t.Substring(0, $eq).Trim()
        $v = $t.Substring($eq + 1).Trim()
        if ($v.Length -eq 0) { continue }

        if ($inList) {
            [void]$Out[$inList].Add($v)
            continue
        }

        if ($k -ieq 'Projectile') {
            $curIsWeapon = $true
            [void]$Out.projectiles.Add($v)
        } elseif ($k -ieq 'Warhead') {
            [void]$Out.warheads.Add($v)
        } elseif ($k -ieq 'Primary' -or $k -ieq 'Secondary' -or
                  $k -ieq 'ElitePrimary' -or $k -ieq 'EliteSecondary') {
            [void]$Out.weaponRefs.Add($v)
        }
    }

    if ($curIsWeapon -and $cur) { [void]$Out.weapons.Add($cur) }
}

function New-Set { New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase) }

$inv = @{
    weapons   = New-Set
    weaponRefs = New-Set
    warheads  = New-Set
    projectiles = New-Set
    infantry  = New-Set
    vehicles  = New-Set
    aircraft  = New-Set
    buildings = New-Set
    superweapons = New-Set
    animations = New-Set
    countries = New-Set
    colors    = New-Set
}

# List sections of rulesmd.ini -> where their values go.
$listSections = @{
    'infantrytypes'    = 'infantry'
    'vehicletypes'     = 'vehicles'
    'aircrafttypes'    = 'aircraft'
    'buildingtypes'    = 'buildings'
    'warheads'         = 'warheads'
    'superweapontypes' = 'superweapons'
    'animations'       = 'animations'
    'countries'        = 'countries'
    'colors'           = 'colors'
}

Write-Host "reading $rulesPath"
Get-RulesInventory -Path $rulesPath -ListSections $listSections -Out $inv

# Art section names double as animation names in practice (AnimType is looked up
# in artmd.ini first). Add the ones that are not already listed in [Animations].
Write-Host "reading $artPath"
$artLines = [System.IO.File]::ReadAllLines($artPath, $latin1)
foreach ($line in $artLines) {
    $t = $line.Trim()
    if ($t.Length -lt 3) { continue }
    if ($t[0] -ne '[') { continue }
    $close = $t.IndexOf(']')
    if ($close -le 1) { continue }
    [void]$inv.animations.Add($t.Substring(1, $close - 1).Trim())
}

# Weapons that are only ever referenced by a unit (no Projectile= seen) still
# belong in the picker - a missing name is worse than an extra one.
foreach ($w in $inv.weaponRefs) { [void]$inv.weapons.Add($w) }

$weapons   = @($inv.weapons   | Sort-Object)
$warheads  = @($inv.warheads  | Sort-Object)
$projectiles = @($inv.projectiles | Sort-Object)
$anims     = @($inv.animations | Sort-Object)
$infantry  = @($inv.infantry  | Sort-Object)
$vehicles  = @($inv.vehicles  | Sort-Object)
$aircraft  = @($inv.aircraft  | Sort-Object)
$buildings = @($inv.buildings | Sort-Object)
$supers    = @($inv.superweapons | Sort-Object)
$countries = @($inv.countries | Sort-Object)

Write-Host ("  weapons      : {0}" -f $weapons.Count)
Write-Host ("  warheads     : {0}" -f $warheads.Count)
Write-Host ("  projectiles  : {0}" -f $projectiles.Count)
Write-Host ("  animations   : {0}" -f $anims.Count)
Write-Host ("  infantry     : {0}" -f $infantry.Count)
Write-Host ("  vehicles     : {0}" -f $vehicles.Count)
Write-Host ("  aircraft     : {0}" -f $aircraft.Count)
Write-Host ("  buildings    : {0}" -f $buildings.Count)
Write-Host ("  superweapons : {0}" -f $supers.Count)
Write-Host ("  countries    : {0}" -f $countries.Count)

# --- serialise -------------------------------------------------------------
# JS array literals are valid JSON, so one rendering serves both the injected
# script and the saved data\gamedata.json used for inspection.
function To-JsArray([string[]]$values) {
    if (-not $values -or $values.Count -eq 0) { return '[]' }
    return '[' + (($values | ForEach-Object {
        '"' + ($_ -replace '\\', '\\' -replace '"', '\"') + '"'
    }) -join ',') + ']'
}

$json = @"
{
  "source": "$($GameDir -replace '\\', '\\')",
  "weapons": $(To-JsArray $weapons),
  "warheads": $(To-JsArray $warheads),
  "projectiles": $(To-JsArray $projectiles),
  "animations": $(To-JsArray $anims),
  "infantry": $(To-JsArray $infantry),
  "vehicles": $(To-JsArray $vehicles),
  "aircraft": $(To-JsArray $aircraft),
  "buildings": $(To-JsArray $buildings),
  "superweapons": $(To-JsArray $supers),
  "countries": $(To-JsArray $countries)
}
"@

$dataDir = Join-Path $here 'data'
New-Item -ItemType Directory -Force $dataDir | Out-Null
$dataJsonPath = Join-Path $dataDir 'gamedata.json'
[System.IO.File]::WriteAllText($dataJsonPath, $json, (New-Object System.Text.UTF8Encoding($false)))
Write-Host "wrote $dataJsonPath"

# The injected form: `var KARST_DATA = {...};`
$injection = "var KARST_DATA = $($json.Trim());"

# --- inline the block library ---------------------------------------------
# ORDER IS A DEPENDENCY ORDER, and getting it wrong fails quietly.
# codegen.js resolves its dependencies at *load* time in the browser
# (`factory(root.KarstRegistry, root.KarstMetadata)`), unlike Node where
# require() happens whenever it likes. With metadata.js listed after codegen.js
# the page still loaded, and then threw "Cannot read properties of undefined
# (reading 'encode')" the first time it generated code - while the build had
# reported success. tests\check_page.js now runs the inlined library and catches
# exactly this.
#   registry.js  - no dependencies
#   metadata.js  - no dependencies
#   codegen.js   - needs registry + metadata
#   validate.js  - needs registry
$blockFiles = @(
    'registry.js',
    'metadata.js',
    'codegen.js',
    'validate.js'
)

$blocksJs = New-Object System.Text.StringBuilder
foreach ($bf in $blockFiles) {
    $p = Join-Path (Join-Path $here 'blocks') $bf
    if (-not (Test-Path $p)) { throw "missing block library file: $p" }
    $src = [System.IO.File]::ReadAllText($p, [System.Text.Encoding]::UTF8)

    # The page must not see `module.exports` lines (they are Node-only), but
    # leaving them in is harmless: `module` is merely undefined. Guarded by
    # typeof in the files themselves, so nothing is stripped here.
    [void]$blocksJs.AppendLine("/* ===== blocks/$bf ===== */")
    [void]$blocksJs.AppendLine($src)
    [void]$blocksJs.AppendLine("")
}

$html = [System.IO.File]::ReadAllText($template, [System.Text.Encoding]::UTF8)

if ($html.IndexOf('/*__BLOCKS__*/') -lt 0) { throw "template has no /*__BLOCKS__*/ marker" }
if ($html.IndexOf('/*__GAMEDATA__*/') -lt 0) { throw "template has no /*__GAMEDATA__*/ marker" }

$html = $html.Replace('/*__BLOCKS__*/', $blocksJs.ToString())
$html = $html.Replace('/*__GAMEDATA__*/', $injection)

$stamp = "数据来源：$GameDir（武器 $($weapons.Count)、弹头 $($warheads.Count)、动画 $($anims.Count)、单位/建筑 $(($infantry.Count + $vehicles.Count + $aircraft.Count + $buildings.Count))）"
$html = $html.Replace('<!--__STAMP__-->', "<!-- $stamp -->")
$html = $html.Replace('__STAMP_TEXT__', $stamp)

[System.IO.File]::WriteAllText($OutFile, $html, (New-Object System.Text.UTF8Encoding($false)))

$size = (Get-Item $OutFile).Length
Write-Host ("wrote {0} ({1:N0} bytes)" -f $OutFile, $size)
Write-Host "双击这个文件即可打开工作室。"
