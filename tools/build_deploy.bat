@echo off
rem ---------------------------------------------------------------------------
rem  Karst: build (Release x86) + hook checks + deploy, in one double-click.
rem
rem  This is only a wrapper: the work is in build_deploy.ps1 next to it. It
rem  exists because this machine's execution policy blocks unsigned .ps1 files,
rem  so the script has to be started with -ExecutionPolicy Bypass.
rem
rem  Optional arguments are passed straight through:
rem      build_deploy.bat -Rebuild     full rebuild
rem      build_deploy.bat -NoDeploy    build + validate only
rem ---------------------------------------------------------------------------
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_deploy.ps1" %*
echo.
pause
