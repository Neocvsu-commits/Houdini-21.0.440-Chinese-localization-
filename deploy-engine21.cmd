@echo off
rem Deploy the freshly built engine into the installed HoudiniChinese package.
rem Keep this file ASCII-only: cmd.exe reads it with the OEM codepage.
setlocal EnableExtensions
cd /d "%~dp0"

set "SRC=build\houdini21\package\HoudiniChinese"
set "DST=%USERPROFILE%\Documents\houdini21.0\packages\HoudiniChinese"

if not exist "%SRC%\bin\HoudiniChineseEngine.dll" (
  echo Build output not found: %SRC%
  echo Run build21.cmd first.
  exit /b 1
)

tasklist /FI "IMAGENAME eq houdini.exe" 2>nul | find /I "houdini.exe" >nul
if not errorlevel 1 (
  echo Houdini is still running. Quit it completely, then run this again.
  exit /b 1
)

set "STAMP=%DATE:~0,4%%DATE:~5,2%%DATE:~8,2%-%TIME:~0,2%%TIME:~3,2%%TIME:~6,2%"
set "STAMP=%STAMP: =0%"
set "BACKUP=%USERPROFILE%\Documents\houdini21.0\HoudiniChinese-backups\engine-deploy-%STAMP%"
if not exist "%BACKUP%" mkdir "%BACKUP%"

echo Backing up current binaries to %BACKUP%
copy /Y "%DST%\bin\HoudiniChineseEngine.dll" "%BACKUP%\" >nul 2>nul
copy /Y "%DST%\HoudiniChineseHook.dll" "%BACKUP%\" >nul 2>nul
copy /Y "%DST%\engine.json" "%BACKUP%\" >nul 2>nul

echo Deploying engine...
copy /Y "%SRC%\bin\HoudiniChineseEngine.dll" "%DST%\bin\HoudiniChineseEngine.dll" >nul || goto :fail
copy /Y "%SRC%\HoudiniChineseHook.dll" "%DST%\HoudiniChineseHook.dll" >nul || goto :fail
copy /Y "%SRC%\engine.json" "%DST%\engine.json" >nul || goto :fail

echo Done. Backup: %BACKUP%
echo Next: python tools\clear_help_cache21.py, then start Houdini.
exit /b 0

:fail
echo Copy failed. Make sure Houdini is fully closed.
exit /b 1
