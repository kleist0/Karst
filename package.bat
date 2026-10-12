@echo off
rem ===========================================================================
rem  Karst packaging - build BOTH halves and pack them into one folder.
rem
rem  WHAT GOES INTO THE PACKAGE (this is the part people get wrong)
rem
rem    C++ half   Karst.dll                                ~263 KB
rem               the Syringe-injected extension itself.
rem
rem    C# half    KarstPatcher\  = 22 DLLs + 2 .json       ~16.7 MB
rem               the script HOST: the Roslyn compiler plus KarstPatcher /
rem               KarstYRpp / KarstExtension and their .NET dependencies.
rem               WITHOUT THIS FOLDER every .cs a user writes is SILENTLY
rem               ignored - no error, no log line. That is why a package made
rem               of Karst.dll alone (what the old package.ps1 produced) is a
rem               broken package, not a small one.
rem
rem    also       3 ini files (karst.ini / karst.hotreload.ini / karst.scripts.ini)
rem               2 editor pages (KarstScriptStudio.html / script-editor.html)
rem               docs\  +  an install README for the player
rem
rem    NOT included: the game, Syringe.exe, Ares/Phobos/Kratos - the player
rem    already has those.
rem
rem  WHERE THE RESULT GOES
rem    one folder per run, inside the package folder in the REPO ROOT, named
rem    with a timestamp, e.g.  <repo>\<package folder>\2026-10-12-10-08-01\
rem    (the package folder's own name is Chinese; the script prints the exact
rem    path when it finishes. It does NOT pop a window open - pass -Open if you
rem    want it to.)
rem
rem    Copy what is inside that folder into your game folder, or zip it up
rem    yourself if you are sending it to somebody.
rem    NO .zip IS PRODUCED - the folder is the deliverable. Pass -Zip if you want
rem    one anyway.
rem
rem  IT DOES NOT COPY ANYTHING INTO A GAME FOLDER. Installing is a separate,
rem  manual step, because whoever installs decides where their game is. For your
rem  own test install there is tools\deploy_to_test_game.bat - that one is a
rem  developer tool and must never be handed to a player.
rem
rem  Usage:
rem     package.bat                 build both halves, then pack the folder
rem     package.bat -NoBuild        package what is already built
rem     package.bat -NoCpp          skip the C++ build
rem     package.bat -NoCSharp       skip the C# build
rem     package.bat -Zip            also produce a .zip next to the folder
rem     package.bat -Open           open the output folder when done
rem
rem  English-only on purpose: cmd.exe parses .bat files with the system ANSI code
rem  page, so Chinese in here corrupts the rest of the script. That actually
rem  happened once - one Chinese file name in a comment broke every later line.
rem ===========================================================================
setlocal

set "TOOLS=%~dp0tools\"

if not exist "%TOOLS%package_full.ps1" (
  echo [X] cannot find "%TOOLS%package_full.ps1"
  echo.
  pause
  exit /b 1
)

echo ============================================================
echo  Karst packaging
echo ============================================================
echo   C++ half : Karst.dll                           ~263 KB
echo   C#  half : KarstPatcher\  22 DLLs + 2 json       ~16.7 MB
echo              ^<-- the script host. Without it, any .cs a
echo                  user writes is silently ignored.
echo   also     : 3 ini + 2 editor pages + docs + install README
echo   output   : one timestamped folder per run, no zip
echo ============================================================
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%TOOLS%package_full.ps1" %*
set RC=%ERRORLEVEL%

echo.
if not "%RC%"=="0" (
  echo [X] packaging FAILED ^(exit %RC%^). Read the messages above.
  echo.
  pause
  exit /b %RC%
)

echo [OK] packaged - see the folder path printed above.
echo      Copy its contents into your game folder, or zip it yourself to share.
echo.
echo Nothing was copied into any game folder.
echo.
pause
endlocal
exit /b 0