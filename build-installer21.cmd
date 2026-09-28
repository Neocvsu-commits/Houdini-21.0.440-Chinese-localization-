@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\build_installer21.ps1"
if errorlevel 1 exit /b %errorlevel%
endlocal
