@echo off
setlocal
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "TESTOUT=%ROOT%\build\tests\import_hooks"
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo [ERROR] vcvars64.bat not found.
    exit /b 1
)
if not exist "%TESTOUT%" mkdir "%TESTOUT%"
call "%VCVARS%" >nul
if errorlevel 1 exit /b 1

cl /nologo /MD /O2 /W4 /WX /EHsc /std:c++17 /utf-8 /LD ^
   "%ROOT%\tests\cpp\import_hook_target.cpp" ^
   /Fo:"%TESTOUT%\import_hook_target.obj" ^
   /Fe:"%TESTOUT%\import_hook_target.dll" ^
   /link /IMPLIB:"%TESTOUT%\import_hook_target.lib"
if errorlevel 1 exit /b 1

cl /nologo /MD /O2 /W4 /WX /EHsc /std:c++17 /utf-8 /LD ^
   "%ROOT%\tests\cpp\import_hook_consumer.cpp" ^
   /Fo:"%TESTOUT%\import_hook_consumer.obj" ^
   /Fe:"%TESTOUT%\import_hook_consumer.dll" ^
   /link "%TESTOUT%\import_hook_target.lib" ^
   /IMPLIB:"%TESTOUT%\import_hook_consumer.lib"
if errorlevel 1 exit /b 1

cl /nologo /MD /O2 /W4 /WX /EHsc /std:c++17 /utf-8 /LD /DCONSUMER_BIAS=200 ^
   "%ROOT%\tests\cpp\import_hook_consumer.cpp" ^
   /Fo:"%TESTOUT%\import_hook_late_consumer.obj" ^
   /Fe:"%TESTOUT%\import_hook_late_consumer.dll" ^
   /link "%TESTOUT%\import_hook_target.lib" ^
   /IMPLIB:"%TESTOUT%\import_hook_late_consumer.lib"
if errorlevel 1 exit /b 1

cl /nologo /MD /O2 /W4 /WX /EHsc /std:c++17 /utf-8 /LD ^
   "%ROOT%\tests\cpp\import_hook_no_imports.cpp" ^
   /Fo:"%TESTOUT%\import_hook_no_imports.obj" ^
   /Fe:"%TESTOUT%\import_hook_no_imports.dll" ^
   /link /NOENTRY /NODEFAULTLIB /IMPLIB:"%TESTOUT%\import_hook_no_imports.lib"
if errorlevel 1 exit /b 1

cl /nologo /MD /O2 /W4 /WX /EHsc /std:c++17 /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN ^
   "%ROOT%\tests\cpp\import_hook_test.cpp" ^
   /Fo:"%TESTOUT%\import_hook_test.obj" ^
   /Fe:"%TESTOUT%\import_hook_test.exe" /link Psapi.lib
if errorlevel 1 exit /b 1

"%TESTOUT%\import_hook_test.exe"
exit /b %ERRORLEVEL%
