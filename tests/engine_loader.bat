@echo off
setlocal
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "OUT=%ROOT%\build\tests\loader"
if not defined HFS set "HFS=C:\Program Files\Side Effects Software\Houdini 22.0.368"
if not exist "%OUT%" mkdir "%OUT%"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if not "%errorlevel%"=="0" exit /b 1
rc /nologo /fo "%OUT%\fixture.res" "%ROOT%\tests\cpp\engine_fixture.rc"
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /MT /W4 /WX /O2 /LD "%ROOT%\tests\cpp\engine_fixture.cpp" /Fo"%OUT%\fixture.obj" /link /OUT:"%OUT%\no_identity.dll" /IMPLIB:"%OUT%\fixture.lib"
if not "%errorlevel%"=="0" exit /b 1
link /nologo /DLL "%OUT%\fixture.obj" "%OUT%\fixture.res" /OUT:"%OUT%\engine_fixture.dll" /IMPLIB:"%OUT%\fixture.lib"
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /MT /W4 /WX /O2 /EHsc /std:c++17 /utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_WIN32_WINNT=0x0602 ^
   "%ROOT%\tests\cpp\engine_loader_test.cpp" /Fo"%OUT%\test.obj" /Fe"%OUT%\engine_loader_test.exe" /link version.lib
if not "%errorlevel%"=="0" exit /b 1
"%OUT%\engine_loader_test.exe" "%OUT%\engine_fixture.dll" "%OUT%\no_identity.dll" "%HFS%\bin\Qt6Core.dll"
exit /b %errorlevel%
