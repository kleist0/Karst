@echo off
rem ---------------------------------------------------------------------------
rem  OBSOLETE ENTRY POINT - kept only so old habits cannot produce a broken package.
rem
rem  This used to run package.ps1, which packages ONLY Karst.dll. A Karst without
rem  the KarstPatcher\ folder has no working C# script system, and the failure is
rem  silent: the user's .cs files are simply ignored at runtime.
rem
rem  It now forwards to package_full.ps1. The entry point to use is ..\package.bat
rem  (or this one - they do the same thing now).
rem ---------------------------------------------------------------------------
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0package_full.ps1" %*
set RC=%ERRORLEVEL%
echo.
pause
endlocal
exit /b %RC%
