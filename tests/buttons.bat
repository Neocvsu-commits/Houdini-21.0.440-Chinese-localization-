@echo off
setlocal
set "ROOT=%~dp0.."
set "QT=%QT6_BASELINE%"
set "HFS=D:\DCC SoftWare\Houdini 21.0.440"
if exist "%QT%\lib\Qt6Test.lib" (
  set "QTINC=%QT%\include"
  set "QTLIB=%QT%\lib"
  set "QTBIN=%QT%\bin"
) else (
  set "QTINC=%HFS%\toolkit\include"
  set "QTLIB=%HFS%\custom\houdini\dsolib"
  set "QTBIN=%HFS%\bin"
  set "INCLUDE=%ROOT%\third_party\qt-6.5.3-headers;%INCLUDE%"
)
if not exist "%QTLIB%\Qt6Widgets.lib" exit /b 1
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist "%ROOT%\build\tests" mkdir "%ROOT%\build\tests"
set "PATH=%QTBIN%;%PATH%"
if exist "%HFS%\bin\Qt_plugins\platforms" set "QT_QPA_PLATFORM_PLUGIN_PATH=%HFS%\bin\Qt_plugins\platforms"
cl /nologo /MD /O2 /W4 /WX /EHsc /std:c++17 /Zc:__cplusplus /permissive- /utf-8 ^
   /DQT_NO_KEYWORDS /DQT_WIDGETS_LIB /DQT_GUI_LIB /I"%QTINC%" ^
   "%ROOT%\tests\cpp\menu_buttons_test.cpp" ^
   /Fo:"%ROOT%\build\tests\menu_buttons_test.obj" ^
   /Fe:"%ROOT%\build\tests\menu_buttons_test.exe" ^
   /link /LIBPATH:"%QTLIB%" Qt6Core.lib Qt6Gui.lib Qt6Widgets.lib
if errorlevel 1 exit /b 1
"%ROOT%\build\tests\menu_buttons_test.exe" -platform offscreen
exit /b %errorlevel%
