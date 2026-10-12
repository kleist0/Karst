@echo off
rem ===========================================================================
rem  Deploy Karst to a test game folder - one double-click.
rem
rem  WHAT IT DOES (all of it is package_full.ps1, this is just a friendly shell)
rem    1. refuses to run while gamemd.exe is running (the DLL would be locked)
rem    2. builds BOTH halves: C++ via MSBuild, C# via build_refs.bat
rem    3. validates the hooks (hookcheck + hookconflict) and REFUSES to deploy
rem       if a hook overlaps another extension's - that has crashed the game
rem    4. copies into the game folder:
rem         Karst.dll + karst.ini + karst.hotreload.ini + karst.scripts.ini
rem         KarstPatcher\  (22 DLLs + compiler.config.json + karstpatcher.config.json)
rem    5. also writes a distributable zip into the package folder in the repo root
rem
rem  It does NOT touch KarstPatcher\Scripts\ - your own .cs files stay put.
rem
rem  Usage:
rem    deploy_to_test_game.bat                       default folder (see GAME below)
rem    deploy_to_test_game.bat "D:\some\game"        another folder
rem    deploy_to_test_game.bat -NoBuild              deploy what is already built
rem
rem  English-only on purpose: cmd.exe parses .bat with the system ANSI code page,
rem  so Chinese text in here would come out as garbage.
rem ===========================================================================
setlocal

set "TOOLS=%~dp0"
set "PROJ=%~dp0.."

rem ---- which game folder, and do we rebuild? --------------------------------
set "GAME=E:\_game\RA2\yr_karst_test_2"
set "EXTRA="
if /i "%~1"=="-NoBuild" (
  set "EXTRA=-NoBuild"
) else (
  if not "%~1"=="" (
    set "GAME=%~1"
    if not "%~2"=="" set "EXTRA=%~2"
  )
)

echo ============================================================
echo  Karst deploy
echo ============================================================
echo  project : %PROJ%
echo  game    : %GAME%
if defined EXTRA echo  options : %EXTRA%
echo ============================================================
echo.

rem ---- preflight ------------------------------------------------------------
tasklist /FI "IMAGENAME eq gamemd.exe" 2>nul | find /I "gamemd.exe" >nul
if not errorlevel 1 (
  echo [X] gamemd.exe is still running.
  echo     Close the game first - the DLL file is locked while it runs.
  echo.
  pause
  exit /b 1
)

if not exist "%GAME%\gamemd.exe" (
  echo [X] "%GAME%" does not look like a game folder - there is no gamemd.exe in it.
  echo.
  pause
  exit /b 1
)

if not exist "%TOOLS%package_full.ps1" (
  echo [X] cannot find "%TOOLS%package_full.ps1"
  echo.
  pause
  exit /b 1
)

rem ---- do it ----------------------------------------------------------------
powershell -NoProfile -ExecutionPolicy Bypass -File "%TOOLS%package_full.ps1" -Deploy "%GAME%" %EXTRA%
set RC=%ERRORLEVEL%

echo.
if not "%RC%"=="0" (
  echo [X] deploy FAILED ^(exit %RC%^). Read the messages above.
  echo.
  pause
  exit /b %RC%
)

echo [OK] deployed to %GAME%
echo.
echo Next steps:
echo   1. Want karst.log? Edit "%GAME%\karst.ini" and set DebugLog=yes
echo      ^(the default is no, so by default no karst.log is written^).
echo   2. Start the game with the launcher .bat already in that folder,
echo      AS ADMINISTRATOR ^(right-click -^> Run as administrator^).
echo      Without elevation Syringe exits immediately with
echo      "No or invalid command line arguments given".
echo   3. To confirm it worked, look at:
echo        %GAME%\karst.log                    -^> "Karst started", "First frame reached"
echo        %GAME%\KarstPatcher\patcher.log     -^> "...**\*.cs: first action complete!"
echo.
pause
endlocal
exit /b 0
