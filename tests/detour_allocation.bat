@echo off
setlocal
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist "%ROOT%\build\tests" mkdir "%ROOT%\build\tests"
pushd "%ROOT%\build\tests"
cl /nologo /O2 /MD /EHsc /std:c++17 /Zc:gotoScope- /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /I"%ROOT%\source\detours" "%ROOT%\tests\cpp\detour_allocation_test.cpp" "%ROOT%\source\detours\disasm.cpp" "%ROOT%\source\detours\modules.cpp" /Fe:detour_allocation_test.exe
if errorlevel 1 exit /b 1
detour_allocation_test.exe
exit /b %errorlevel%
