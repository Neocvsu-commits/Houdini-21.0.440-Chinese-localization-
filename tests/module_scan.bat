@echo off
setlocal
set "ROOT=%~dp0.."
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist "%ROOT%\build\tests" mkdir "%ROOT%\build\tests"
cl /nologo /MD /O2 /W4 /WX /EHsc /std:c++17 /utf-8 ^
   "%ROOT%\tests\cpp\module_scan_state_test.cpp" ^
   /Fo:"%ROOT%\build\tests\module_scan_state_test.obj" ^
   /Fe:"%ROOT%\build\tests\module_scan_state_test.exe"
if errorlevel 1 exit /b 1
"%ROOT%\build\tests\module_scan_state_test.exe"
exit /b %errorlevel%
