@echo off
setlocal
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "QT=%QT6_BASELINE%"
if not exist "%QT%\lib\Qt6Core.lib" set "QT=%ROOT%\..\_ThirdParty\Qt\6.8.3\msvc2022_64"
if not exist "%QT%\lib\Qt6Core.lib" set "QT=%ROOT%\third_party\qt6sdk"
if not exist "%QT%\lib\Qt6Core.lib" exit /b 1
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "PATH=%QT%\bin;%PATH%"
if not exist "%ROOT%\build\tests" mkdir "%ROOT%\build\tests"
cl /nologo /MD /O2 /W4 /WX /EHsc /std:c++17 /Zc:__cplusplus /permissive- /utf-8 ^
   /DWIN32 /DNOMINMAX /DQT_NO_KEYWORDS /I"%QT%\include" ^
   "%ROOT%\tests\cpp\display_text_layout_test.cpp" ^
   /Fo:"%ROOT%\build\tests\display_text_layout_test.obj" ^
   /Fe:"%ROOT%\build\tests\display_text_layout_test.exe" ^
   /link /LIBPATH:"%QT%\lib" Qt6Core.lib Qt6Gui.lib
if errorlevel 1 exit /b 1
"%ROOT%\build\tests\display_text_layout_test.exe" -platform offscreen
exit /b %errorlevel%
