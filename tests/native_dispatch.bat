@echo off
setlocal
set "ROOT=%~dp0.."
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist "%ROOT%\build\tests" mkdir "%ROOT%\build\tests"
cl /nologo /MD /O2 /W4 /WX /EHsc /std:c++17 /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN ^
   "%ROOT%\tests\cpp\native_dispatch_hook_test.cpp" ^
   /Fo:"%ROOT%\build\tests\native_dispatch_hook_test.obj" ^
   /Fe:"%ROOT%\build\tests\native_dispatch_hook_test.exe"
if errorlevel 1 exit /b 1
"%ROOT%\build\tests\native_dispatch_hook_test.exe"
exit /b %errorlevel%
