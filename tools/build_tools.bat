@echo off
rem ===========================================================================
rem  Build the standalone verification tools (32-bit, no game required).
rem ===========================================================================
rem  WHY THIS SCRIPT EXISTS
rem
rem  The tools link real project sources, and those sources include YRpp headers,
rem  which only compile with the same language settings the project uses
rem  (/std:c++latest plus the project's preprocessor definitions). Typing those
rem  options by hand every time is how the tools drifted out of sync with the
rem  build, so they live here.
rem
rem  Usage:  tools\build_tools.bat              build every tool
rem          tools\build_tools.bat json         build only check_json.exe
rem          tools\build_tools.bat targets      build only check_targets.exe
rem          tools\build_tools.bat snapshot     build only check_snapshot.exe
rem          tools\build_tools.bat watch        build only check_watch.exe
rem          tools\build_tools.bat types        build only check_types.exe
rem
rem  Output goes next to this script.
rem ===========================================================================

setlocal
set "VCVARS=D:\software\_Study\Microsoft\Visual_Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat"
if not exist "%VCVARS%" (
  echo Missing vcvars32.bat: %VCVARS%
  exit /b 1
)

call "%VCVARS%" >nul
cd /d "%~dp0"

set SRC=..\src
set YRPP=..\YRpp
rem NOTE: HAS_EXCEPTIONS=0 is deliberately NOT set here. The project builds with
rem it, but YRpp's Memory.h uses a parameter pack in a way that only compiles
rem with exceptions enabled (C3520 otherwise); the tools never throw, so the
rem setting is irrelevant to them.
set DEFS=/DSYR_VER=2 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0601 /DNTDDI_VERSION=0x06010000
set FLAGS=/nologo /EHsc /W3 /std:c++latest /I"%SRC%" /I"%YRPP%" %DEFS%
set LIBS=kernel32.lib user32.lib advapi32.lib

if "%~1"=="" goto all
if /i "%~1"=="all" goto all
if /i "%~1"=="json" goto json
if /i "%~1"=="scripts" goto scripts
if /i "%~1"=="targets" goto targets
if /i "%~1"=="snapshot" goto snapshot
if /i "%~1"=="watch" goto watch
if /i "%~1"=="types" goto types
echo Unknown target: %~1
exit /b 1

:all
call :dojson || exit /b 1
call :doscripts || exit /b 1
call :dotargets || exit /b 1
call :dosnapshot || exit /b 1
call :dowatch || exit /b 1
call :dotypes || exit /b 1
echo.
echo All tools built.
exit /b 0

:json
call :dojson || exit /b 1
exit /b 0

:scripts
call :doscripts || exit /b 1
exit /b 0

:targets
call :dotargets || exit /b 1
exit /b 0

:snapshot
call :dosnapshot || exit /b 1
exit /b 0

:watch
call :dowatch || exit /b 1
exit /b 0

:types
call :dotypes || exit /b 1
exit /b 0

:dojson
echo === check_json.exe ===
cl %FLAGS% /Fe:check_json.exe check_json.cpp "%SRC%\Script\Json.cpp" /link %LIBS%
if errorlevel 1 exit /b 1
exit /b 0

:doscripts
echo === check_scripts.exe ===
cl %FLAGS% /Fe:check_scripts.exe check_scripts.cpp "%SRC%\Script\Json.cpp" "%SRC%\Script\ScriptLoader.cpp" "%SRC%\Config\INIConfigReader.cpp" /link %LIBS%
if errorlevel 1 exit /b 1
exit /b 0

:dotargets
echo === check_targets.exe ===
cl %FLAGS% /Fe:check_targets.exe check_targets.cpp "%SRC%\Ext\IniHotReload\IniReloadTarget.cpp" "%SRC%\Config\INIConfigReader.cpp" "%SRC%\Config\KarstConfig.cpp" "%SRC%\Core\Log.cpp" /link %LIBS%
if errorlevel 1 exit /b 1
exit /b 0

:dosnapshot
echo === check_snapshot.exe ===
cl %FLAGS% /Fe:check_snapshot.exe check_snapshot.cpp "%SRC%\Ext\IniHotReload\IniSnapshot.cpp" /link %LIBS%
if errorlevel 1 exit /b 1
exit /b 0

:dowatch
echo === check_watch.exe ===
cl %FLAGS% /Fe:check_watch.exe check_watch.cpp "%SRC%\Ext\IniHotReload\IniWatchThread.cpp" "%SRC%\Ext\IniHotReload\IniReloadTarget.cpp" "%SRC%\Config\INIConfigReader.cpp" "%SRC%\Config\KarstConfig.cpp" "%SRC%\Core\Log.cpp" /link %LIBS%
if errorlevel 1 exit /b 1
exit /b 0

:dotypes
echo === check_types.exe ===
cl %FLAGS% /Fe:check_types.exe check_types.cpp "%SRC%\Ext\IniHotReload\IniReloadTarget.cpp" "%SRC%\Config\INIConfigReader.cpp" "%SRC%\Config\KarstConfig.cpp" "%SRC%\Core\Log.cpp" /link %LIBS%
if errorlevel 1 exit /b 1
exit /b 0
