# ===========================================================================
#  Karst: build BOTH halves -> validate -> package a complete distributable zip
# ===========================================================================
#  WHY THIS EXISTS NEXT TO package.ps1
#
#  package.ps1 predates the C# script system. It packages only Karst.dll, so a
#  zip built with it silently ships a Karst whose scripting is dead: ClrHost
#  looks for <game>\karstpatcher\, finds nothing, and every .cs the user writes
#  is quietly ignored. Measured on 2026-10-12:
#
#     dist\Karst-0.1.0-20261011-0442.zip contains Karst.dll (112,128 B)
#     current Release\Karst.dll is                                  263,168 B
#
#  ...i.e. that zip is from before the script system, and its README-安装.txt
#  still lists only the three original features.
#
#  WHAT A COMPLETE KARST NEEDS (verified against the working test install)
#
#     <game>\Karst.dll                      the C++ extension (Syringe loads it)
#     <game>\karst.ini                      its own configuration
#     <game>\karst.hotreload.ini            INI hot-reload target list
#     <game>\karst.scripts.ini              JSON appearance-script settings
#     <game>\karstpatcher\*.dll             the C# host + Roslyn + deps (22 DLLs)
#     <game>\karstpatcher\compiler.config.json     \
#     <game>\karstpatcher\karstpatcher.config.json  > NOT in the repo - see below
#     <game>\karstpatcher\Scripts\*.cs      the user's scripts (created by them)
#
#  The two .json files exist ONLY in the game install: they were hand-made
#  during development, so a fresh clone cannot reproduce a working install from
#  the repo alone. This script therefore WRITES them (their content is fixed and
#  is reproduced verbatim below), which is what makes the zip self-sufficient.
#
#  Usage:
#     powershell -ExecutionPolicy Bypass -File Karst\tools\package_full.ps1
#     powershell -ExecutionPolicy Bypass -File Karst\tools\package_full.ps1 -NoBuild
#     powershell -ExecutionPolicy Bypass -File Karst\tools\package_full.ps1 -Deploy "E:\_game\RA2\yr_karst_test"
#
#  NOTE (encoding): this file contains Chinese and MUST stay UTF-8 with BOM.
#  Windows PowerShell 5.1 reads .ps1 as ANSI, so without the BOM the Chinese
#  turns into garbage and the parser reports errors that have nothing to do
#  with the real problem.
# ===========================================================================

param(
    [switch]$NoBuild,          # package whatever is already built
    [switch]$NoCpp,            # skip the C++ build
    [switch]$NoCSharp,         # skip the C# (KarstPatcher) build
    [string]$Deploy = '',      # also copy the result into this game folder
    [switch]$Zip,              # also produce a .zip next to the folder
    [string]$OutDir = '',      # where the package lands (default: <repo>\发布包)
    [switch]$Open,             # open the output folder when finished (off by default)
    [string]$GameDir = 'E:\_game\RA2\yr_karst_test_2'
)

$ErrorActionPreference = 'Stop'

$tools   = $PSScriptRoot
$karst   = Split-Path -Parent $tools
$sln     = Join-Path $karst 'Karst.sln'
$dll     = Join-Path $karst 'Release\Karst.dll'
$refs    = Join-Path $karst 'refs\karstpatcher'
$csOut   = Join-Path $refs 'Extension\bin\Release'
# Where the package lands: a folder in the REPO ROOT with a self-explanatory
# name, so you do not have to go hunting inside dist\. (It used to be "dist",
# which is the convention but tells a non-developer nothing.)
$distDir = if ($OutDir) { $OutDir } else { Join-Path $karst '发布包' }

function Step($t) { Write-Host "`n=== $t" -ForegroundColor Cyan }
function Ok($t)   { Write-Host "  $t" -ForegroundColor Green }
function Warn($t) { Write-Host "  $t" -ForegroundColor Yellow }
function Fail($t) { Write-Host "  $t" -ForegroundColor Red; exit 1 }

function Find-MsBuild {
    $candidates = @(
        'D:\software\_Study\Microsoft\Visual_Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe'
    )
    # @() around the PIPELINE, not around the literal: with exactly one hit the
    # pipeline returns a bare string, and $x[0] then yields its FIRST CHARACTER
    # ('D' for "D:\...") - which is exactly what happened on this script's first
    # run. Same fix in Find-Python below.
    $found = @($candidates | Where-Object { Test-Path $_ })
    if ($found.Count -eq 0) { Fail 'MSBuild.exe not found - edit Find-MsBuild in this script.' }
    return $found[0]
}

function Find-Python {
    $candidates = @(
        'C:\Users\Lenovo\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe',
        'C:\Python312\python.exe'
    )
    $found = @($candidates | Where-Object { Test-Path $_ })
    if ($found.Count -gt 0) { return $found[0] }
    $cmd = Get-Command python -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

# --- preflight -------------------------------------------------------------
Step 'preflight'
$msbuild = Find-MsBuild
$python  = Find-Python
Ok "msbuild : $msbuild"
if ($python) { Ok "python  : $python" } else { Warn 'python not found - hook checks will be SKIPPED' }
if (Get-Process gamemd -ErrorAction SilentlyContinue) {
    Warn 'gamemd.exe is running - it will keep the old DLL in memory until restarted'
}

# --- build C++ -------------------------------------------------------------
if (-not $NoBuild -and -not $NoCpp) {
    Step 'build Karst.dll (Release|x86)'
    & $msbuild $sln /t:Build /p:Configuration=Release /p:Platform=x86 /nologo /v:minimal
    if ($LASTEXITCODE -ne 0) { Fail "C++ build failed (exit $LASTEXITCODE)" }
}
if (-not (Test-Path $dll)) { Fail "missing $dll - build first (drop -NoBuild)" }
Step 'artifact'
Ok ("Karst.dll  {0:N0} bytes @ {1}" -f (Get-Item $dll).Length, (Get-Item $dll).LastWriteTime)

# --- build C# --------------------------------------------------------------
if (-not $NoBuild -and -not $NoCSharp) {
    Step 'build the C# reference layers (KarstPatcher / KarstYRpp / KarstExtension)'
    & cmd /c "`"$(Join-Path $refs 'build_refs.bat')`" Release"
    if ($LASTEXITCODE -ne 0) { Fail "C# build failed (exit $LASTEXITCODE)" }
}
foreach ($f in 'KarstPatcher.dll', 'KarstYRpp.dll', 'KarstExtension.dll') {
    if (-not (Test-Path (Join-Path $csOut $f))) { Fail "missing $f in $csOut - run build_refs.bat Release" }
}
Ok ("C# host    {0:N0} bytes" -f (Get-Item (Join-Path $csOut 'KarstPatcher.dll')).Length)

# --- hook gates ------------------------------------------------------------
# Both checks have caught real crashes; see tools\build_deploy.ps1 for the story.
if ($python) {
    Step 'hookcheck (size >= 5, ends on an instruction boundary)'
    & $python (Join-Path $tools 'hookcheck.py') $dll
    if ($LASTEXITCODE -ne 0) { Fail 'hookcheck reported unsafe hooks - NOT packaging' }
    Ok 'all hooks well formed'

    $ext = @('Ares.dll', 'Phobos.dll', 'Kratos.dll') |
        ForEach-Object { Join-Path $GameDir $_ } | Where-Object { Test-Path $_ }
    if ($ext.Count -gt 0) {
        Step 'hookconflict (bytes shared with Ares / Phobos / Kratos)'
        & $python (Join-Path $tools 'hookconflict.py') $dll @ext
        if ($LASTEXITCODE -ne 0) { Fail 'hook overlap - NOT packaging' }
        Ok 'no overlapping hooks'
    } else {
        Step 'hookconflict skipped (no Ares/Phobos/Kratos under the game dir)'
    }
}

# --- version ---------------------------------------------------------------
$header = Get-Content (Join-Path $karst 'src\Version.h') -Raw
# One field at a time: $Matches only holds the result of the LAST -match, so
# chaining these in one condition silently yields "$patch.." instead of a version.
$major = if ($header -match 'KARST_VERSION_MAJOR\s+(\d+)') { $Matches[1] } else { '0' }
$minor = if ($header -match 'KARST_VERSION_MINOR\s+(\d+)') { $Matches[1] } else { '0' }
$patch = if ($header -match 'KARST_VERSION_PATCH\s+(\d+)') { $Matches[1] } else { '0' }
$version = "$major.$minor.$patch"
# The folder name is just a timestamp: it sorts naturally and you can see at a
# glance when a build was made. (It used to be "Karst-<version>-<stamp>";
# $version is still used inside the generated install README.)
$stamp   = Get-Date -Format 'yyyy-MM-dd-HH-mm-ss'
$name    = $stamp

# --- stage -----------------------------------------------------------------
Step "stage -> dist\$name"
$stage = Join-Path $distDir $name
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force (Join-Path $stage 'docs')            | Out-Null
New-Item -ItemType Directory -Force (Join-Path $stage 'karstpatcher')    | Out-Null
New-Item -ItemType Directory -Force (Join-Path $stage 'karstpatcher\Scripts') | Out-Null

# C++ side
Copy-Item $dll (Join-Path $stage 'Karst.dll') -Force
foreach ($ini in 'karst.ini', 'karst.hotreload.ini', 'karst.scripts.ini') {
    $src = Join-Path $karst $ini
    if (Test-Path $src) { Copy-Item $src (Join-Path $stage $ini) -Force }
    else { Warn "missing $ini - skipped" }
}

# C# side: every DLL from the Extension output (it already aggregates the NuGet
# dependencies), plus the two config files that the repo does not carry.
Get-ChildItem $csOut -File -Filter *.dll | ForEach-Object {
    Copy-Item $_.FullName (Join-Path $stage "karstpatcher\$($_.Name)") -Force
}

$compilerConfig = @'
{
    "references" : [
        "System.dll",
        "System.Core.dll",
        "System.Data.dll",
        "KarstPatcher.dll",
        "KarstYRpp.dll",
        "KarstExtension.dll"
    ],
    "preprocessor_symbols" : [
        "REALTIME_INI"
    ],
    "show_hidden" : false,
    "load_temp_file_in_memory" : false,
    "emit_pdb" : true,
    "force_compile" : false,
    "pack_assembly" : true
}
'@
$patcherConfig = @'
{
    "show_attach_window" : false,
    "try_catch_callable" : true,
    "force_gc_collect" : false,
    "hide_console" : true
}
'@
Set-Content -Path (Join-Path $stage 'karstpatcher\compiler.config.json')    -Value $compilerConfig -Encoding UTF8
Set-Content -Path (Join-Path $stage 'karstpatcher\karstpatcher.config.json') -Value $patcherConfig  -Encoding UTF8

# Editors: the user-facing pages, taken from their built copies.
# The appearance editor is BUILT into tools\script-editor\. The repo root used to
# carry a second copy that this line took; that copy was removed (it was a
# duplicate of the build product, and the build script only ever writes the one
# under tools\). So take the built file.
Copy-Item (Join-Path $tools 'script-editor\script-editor.html') (Join-Path $stage 'script-editor.html') -Force
$studioHtml = Join-Path $tools 'script-studio\KarstScriptStudio.html'
if (Test-Path $studioHtml) { Copy-Item $studioHtml (Join-Path $stage 'KarstScriptStudio.html') -Force }
foreach ($d in '已有功能0.1.0.md', '脚本API参考.md') {
    $p = Join-Path $karst "docs\$d"
    if (Test-Path $p) { Copy-Item $p (Join-Path $stage "docs\$d") -Force }
}

# A Scripts placeholder so the folder exists and its purpose is obvious.
$scriptsReadme = @"
把你的 C# 脚本（.cs）放在这个目录里。

Karst 会在游戏启动时、以及每次文件变化时自动编译它们，日志见 ..\karstpatcher.log 。
脚本写好后，在 rulesmd.ini 里给目标单位加一行（值是类名，大小写敏感）：

    [MTNK]
    Scripts=你的类名

改 .cs 不用重启游戏；改 rulesmd.ini 需要重开一局。

不想手写？用同目录的 ..\..\KarstScriptStudio.html（双击打开，用独立浏览器窗口），
搭积木生成脚本；脚本写法看 docs\已有功能0.1.0.md ，可用 API 查 docs\脚本API参考.md 。
"@
Set-Content -Path (Join-Path $stage 'karstpatcher\Scripts\README.txt') -Value $scriptsReadme -Encoding UTF8

$readme = @"
Karst $version - 安装说明
================================================================

一、这是什么
    Karst 是《尤里的复仇》(YR 1.001) 的引擎扩展，通过 Syringe 注入 gamemd.exe。
    当前包含五个功能：

      * ShowByArt / ShowByCountries    一个建筑按【拥有者国家】显示不同外观
      * Sidebar.MixFileIndex           每个【国家】使用不同的侧边栏 MIX
      * CountryArt                     建筑外观、占地与图标的按国家切换
      * IniHotReload                   运行中重读规则 INI，改规则不用重启游戏
      * C# 脚本系统                     用 C# 给单位挂事件（每帧/开火/受伤/出场/移除）

    详细说明见 docs\ 下的文档；脚本写法见 docs\已有功能0.1.0.md ，可用 API 查 docs\脚本API参考.md 。

二、安装
    1. 把 Karst.dll 复制到 gamemd.exe 所在目录
       （也就是有 Syringe.exe / Ares.dll 的那个目录）。
    2. 把 karst.ini、karst.hotreload.ini、karst.scripts.ini 也复制过去。
       三个都可以不要：没有它们就用内置默认值。
    3. 把整个 karstpatcher\ 目录复制到 gamemd.exe 所在目录。
       ⚠️ 少了它，C# 脚本系统不会工作 —— 脚本文件会被静默忽略。
    4. 用 Syringe.exe 启动游戏（或你原来的 Ares 启动方式）：
           Syringe.exe gamemd.exe
       Syringe 会自动加载同目录下所有扩展 DLL。

    最终目录结构应该是：
        <游戏目录>\
            gamemd.exe
            Syringe.exe
            Karst.dll
            karst.ini
            karst.hotreload.ini
            karst.scripts.ini
            karstpatcher\            <- 必须整个复制，里面有 Roslyn 与依赖
                KarstPatcher.dll
                KarstYRpp.dll
                KarstExtension.dll
                compiler.config.json
                karstpatcher.config.json
                ...（其余是 Roslyn 与 .NET 依赖）
                Scripts\             <- 你把 .cs 放这里
            script-editor.html       <- 可选：外观脚本编辑器（双击打开）
            KarstScriptStudio.html   <- 可选：积木式 C# 脚本工作室（双击打开）
            docs\

三、目录里那两个网页怎么用
    * script-editor.html      —— 外观脚本（JSON）可视化编辑 + C# 空骨架生成
    * KarstScriptStudio.html  —— 积木式写 C# 脚本、并管理 Scripts 目录里的脚本

    两个都直接用浏览器打开（双击）。⚠️ 不要在编辑器的"预览面板"里打开：
    那样浏览器会把页面当作沙箱文档，目录选择器与下载都会被拒绝。

四、配置
    * 功能开关写在 rulesmd.ini / artmd.ini 里，见 docs\已有功能0.1.0.md 。
    * Karst 自身的开关在 karst.ini：
          Enabled=yes              总开关
          LogLevel=info            正常玩用 info，排错时改 debug
          DebugLog=yes             额外写 karst.log（排错用）
    * C# 脚本的编译与加载日志在 karstpatcher\karstpatcher.log 。

五、排错
    1. 游戏没反应 / 注入失败：
       确认 gamemd.exe 与 Syringe.exe 都以【管理员身份】运行 —— 未提权时
       Syringe 会直接报 "No or invalid command line arguments given" 并退出。
    2. 脚本没生效：
       看 karstpatcher\karstpatcher.log 。出现 "compile file succeed!" 说明编译成功；
       "error CS...." 是编译错误（带文件与行号）；再看 rulesmd.ini 里的
       Scripts=类名 是否拼对（大小写敏感），并且改完要重开一局。
    3. karstpatcher\karstpatcher.log 根本不存在（脚本系统完全没起来）：
       这是"文件带网络来源标记"造成的 —— 从网盘 / 聊天软件下载的压缩包解压后，
       Windows 会给文件打上 Zone.Identifier，.NET 因此拒绝加载 karstpatcher 里的
       程序集。本版本已在启动时自动清掉该标记，正常情况下**你不需要做任何事**
       （日志里会出现 "removed the 'downloaded from the Internet' mark from ..."）。
       只有在使用更早版本时，才需要手动执行一次：
           Get-ChildItem karstpatcher -Recurse | Unblock-File
    4. 崩溃：
       debug\snapshot-*\except.txt 里有异常地址与寄存器；
       把游戏目录下的 karst.log（需 DebugLog=yes）发给开发者。

六、已知限制
    * 只支持 YR 1.001 的 gamemd.exe（校验和见 Syringe）。
    * CountryArt：读档后外观回到原样；只在建筑建成时判定一次。
    * IniHotReload：只重读 karst.hotreload.ini 里列出的文件；已建成的实例
      不会跟随那些"建造时被复制到实例上"的数值。
"@
Set-Content -Path (Join-Path $stage 'README-安装.txt') -Value $readme -Encoding UTF8

Step 'staged content'
Get-ChildItem $stage -Recurse -File | Sort-Object FullName | ForEach-Object {
    "  {0,-46} {1,10:N0} B" -f $_.FullName.Substring($stage.Length + 1), $_.Length
}
$stageSum = Get-ChildItem $stage -Recurse -File | Measure-Object -Property Length -Sum
Ok ("{0} files, {1:N1} MB" -f $stageSum.Count, ($stageSum.Sum / 1MB))

# --- zip (optional) --------------------------------------------------------
# The FOLDER is the deliverable: a ready-to-drag copy of everything. A zip only
# matters when you hand the package to somebody else, so it is opt-in now.
$zipPath = $null
if ($Zip) {
    Step 'zip'
    $zipPath = Join-Path $distDir "$name.zip"
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zipPath -CompressionLevel Optimal
    Ok ("{0} ({1:N0} KB)" -f $zipPath, ((Get-Item $zipPath).Length / 1KB))
} else {
    Step 'zip skipped (the folder is the deliverable; pass -Zip to also make one)'
}

# --- optional deploy -------------------------------------------------------
if ($Deploy) {
    Step "deploy -> $Deploy"
    if (-not (Test-Path $Deploy)) { Fail "target folder does not exist: $Deploy" }
    if (Get-Process gamemd -ErrorAction SilentlyContinue) { Fail 'gamemd.exe is running - close the game first' }

    Copy-Item $dll (Join-Path $Deploy 'Karst.dll') -Force
    foreach ($ini in 'karst.ini', 'karst.hotreload.ini', 'karst.scripts.ini') {
        $src = Join-Path $karst $ini
        if (Test-Path $src) { Copy-Item $src (Join-Path $Deploy $ini) -Force }
    }
    $tgt = Join-Path $Deploy 'karstpatcher'
    New-Item -ItemType Directory -Force $tgt | Out-Null
    New-Item -ItemType Directory -Force (Join-Path $tgt 'Scripts') | Out-Null
    $i = 0
    Get-ChildItem $csOut -File -Filter *.dll | ForEach-Object {
        Copy-Item $_.FullName (Join-Path $tgt $_.Name) -Force; $i++
    }
    Set-Content -Path (Join-Path $tgt 'compiler.config.json')    -Value $compilerConfig -Encoding UTF8
    Set-Content -Path (Join-Path $tgt 'karstpatcher.config.json') -Value $patcherConfig  -Encoding UTF8
    Ok ("Karst.dll + 3 ini + {0} DLL into karstpatcher\ + 2 config" -f $i)
    Ok ("hash match: {0}" -f ((Get-FileHash $dll).Hash -eq (Get-FileHash (Join-Path $Deploy 'Karst.dll')).Hash))
    Warn 'Scripts\ 目录里的用户脚本没有被覆盖。现在运行 restart_test.bat（或 Syringe）验证。'
}

Write-Host "`npackaged folder: $stage" -ForegroundColor Cyan
if ($zipPath) { Write-Host "zip            : $zipPath" -ForegroundColor Cyan }
Write-Host "parent         : $distDir" -ForegroundColor Cyan

if ($Open) {
    # Opt-in. Packaging is usually run to GET the artifact, not to look at it,
    # and a window popping up on every run just gets in the way - the path is
    # printed above either way, and the folder is right there in the repo root.
    Start-Process explorer.exe $stage
}
