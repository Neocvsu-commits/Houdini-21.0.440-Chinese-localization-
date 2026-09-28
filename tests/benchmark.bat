@echo off
setlocal
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "QT=%QT6_BASELINE%"
if not exist "%QT%\lib\Qt6Core.lib" set "QT=%ROOT%\..\_ThirdParty\Qt\6.8.3\msvc2022_64"
if not exist "%QT%\lib\Qt6Core.lib" exit /b 1
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "PATH=%QT%\bin;%PATH%"
if not exist "%ROOT%\build\benchmarks" mkdir "%ROOT%\build\benchmarks"
if errorlevel 1 exit /b 1
rem QtCore only: no application window, input automation or Houdini process.
cl /nologo /MD /O2 /W4 /WX /EHsc /std:c++17 /Zc:__cplusplus /permissive- /utf-8 ^
   /DQT_NO_KEYWORDS /I"%QT%\include" ^
   "%ROOT%\tests\perf\runtime_benchmark.cpp" ^
   /Fo:"%ROOT%\build\benchmarks\runtime_benchmark.obj" ^
   /Fe:"%ROOT%\build\benchmarks\runtime_benchmark.exe" ^
   /link /LIBPATH:"%QT%\lib" Qt6Core.lib
if errorlevel 1 exit /b 1
"%ROOT%\build\benchmarks\runtime_benchmark.exe" > "%ROOT%\build\audit-runtime-benchmark.txt"
if errorlevel 1 exit /b 1
type "%ROOT%\build\audit-runtime-benchmark.txt"
exit /b 0
