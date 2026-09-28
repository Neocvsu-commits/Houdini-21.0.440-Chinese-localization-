@echo off
setlocal
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "QT=%QT6_BASELINE%"
if not exist "%QT%\lib\Qt6Test.lib" set "QT=%ROOT%\..\_ThirdParty\Qt\6.8.3\msvc2022_64"
if not exist "%QT%\lib\Qt6Test.lib" exit /b 1
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist "%ROOT%\build\tests" mkdir "%ROOT%\build\tests"
set "PATH=%QT%\bin;%PATH%"
cl /nologo /MD /O2 /W4 /WX /EHsc /std:c++17 /Zc:__cplusplus /permissive- /utf-8 ^
   /DQT_NO_KEYWORDS /DQT_WIDGETS_LIB /DQT_GUI_LIB /I"%QT%\include" ^
   "%ROOT%\tests\cpp\shortcut_events_test.cpp" ^
   /Fo:"%ROOT%\build\tests\shortcut_events_test.obj" ^
   /Fe:"%ROOT%\build\tests\shortcut_events_test.exe" ^
   /link /LIBPATH:"%QT%\lib" Qt6Core.lib Qt6Gui.lib Qt6Widgets.lib Qt6Test.lib
if errorlevel 1 exit /b 1
"%ROOT%\build\tests\shortcut_events_test.exe" -platform offscreen
exit /b %errorlevel%
