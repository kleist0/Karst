@echo off
rem ===========================================================================
rem  Build the three reference layers (see README.md), in dependency order:
rem
rem      DynamicPatcher.dll   (runtime: hooks, Roslyn compile, script loading)
rem      PatcherYRpp.dll      (bindings: Pointer<T>, TechnoClass, MapClass, ...)
rem      Extension.dll        (TechnoExt/BulletExt, TechnoScriptable, decorators)
rem
rem  Requirements, both already satisfied on the dev machine:
rem    * .NET Framework 4.8 SDK + Targeting Pack (Visual Studio Installer,
rem      "Individual components"). WITHOUT the targeting pack the build fails
rem      with MSB3644; do not "fix" that by restoring the NuGet
rem      ReferenceAssemblies workaround - it got as far as 6 errors and then hit
rem      an implicit System.Core 3.5 from a leftover developer pack (CS0433).
rem    * Internet on first run, so NuGet can restore Roslyn/Newtonsoft.
rem
rem  Usage:  build_refs.bat [Debug|Release]
rem ===========================================================================
setlocal

set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Release

set HERE=%~dp0

rem --- locate MSBuild --------------------------------------------------------
rem Ask vswhere FIRST: it ships with the Visual Studio Installer and knows where
rem VS actually lives, so this works on any machine. The literal path below is
rem only a fallback for boxes without vswhere.
rem
rem It used to be the only way, which meant a different VS edition, a different
rem drive letter, or a machine without VS on D: broke the build with
rem "[error] MSBuild not found at D:\...".
set MSBUILD=
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
    if not defined MSBUILD set "MSBUILD=%%i"
  )
)
if not defined MSBUILD set "MSBUILD=D:\software\_Study\Microsoft\Visual_Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"

if not exist "%MSBUILD%" (
  echo [error] MSBuild not found.
  echo         Install Visual Studio with the ".NET desktop build tools" workload,
  echo         or edit the fallback path inside this file.
  exit /b 1
)
echo using MSBuild: %MSBUILD%

echo === building reference layers (%CONFIG%) ===

rem DynamicPatcher first: the other two reference its DLL.
call :build DynamicPatcher || exit /b 1
call :build PatcherYRpp   || exit /b 1
call :build Extension     || exit /b 1

echo.
echo === output ===
rem NOTE: the .csproj files set AssemblyName to the Karst-prefixed names, so the
rem products are NOT named after the projects. This listing said
rem "DynamicPatcher.dll / PatcherYRpp.dll / Extension.dll" for a long time - wrong.
echo   %HERE%DynamicPatcher\bin\%CONFIG%\KarstPatcher.dll
echo   %HERE%PatcherYRpp\bin\%CONFIG%\KarstYRpp.dll
echo   %HERE%Extension\bin\%CONFIG%\KarstExtension.dll
echo.
echo === done ===
endlocal
exit /b 0

:build
echo.
echo --- %1 ---
"%MSBUILD%" "%HERE%%1\%1.csproj" /t:Restore,Build /p:Configuration=%CONFIG% /p:Platform=AnyCPU /nologo /v:minimal
if errorlevel 1 (
  echo [error] %1 failed
  exit /b 1
)
exit /b 0
