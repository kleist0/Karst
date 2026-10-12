@echo off
rem ===========================================================================
rem  Opens the Karst script studio in a STANDALONE browser window.
rem
rem  Why a launcher exists at all: opening the HTML from inside an IDE's
rem  preview pane makes the browser treat the page as a sandboxed document, and
rem  then the directory picker, downloads and the clipboard are all refused.
rem  Double-clicking this file always gets a real window.
rem
rem  English-only on purpose: cmd.exe parses .bat files with the system ANSI
rem  code page, so Chinese text in here would come out as garbage.
rem ===========================================================================
setlocal
set "HTML=%~dp0KarstScriptStudio.html"

if not exist "%HTML%" (
  echo [X] Not found: "%HTML%"
  echo     Build it first:
  echo       powershell -ExecutionPolicy Bypass -File "%~dp0build_studio.ps1"
  pause
  exit /b 1
)

set "EDGE=%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe"
if not exist "%EDGE%" set "EDGE=%ProgramFiles%\Microsoft\Edge\Application\msedge.exe"
if not exist "%EDGE%" set "EDGE=%LocalAppData%\Microsoft\Edge\Application\msedge.exe"

if exist "%EDGE%" (
  echo Opening in Edge: "%HTML%"
  start "" "%EDGE%" "%HTML%"
) else (
  echo Edge not found - opening with the default browser.
  start "" "%HTML%"
)

exit /b 0
