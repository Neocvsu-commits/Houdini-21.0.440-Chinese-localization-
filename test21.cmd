@echo off
setlocal
cd /d "%~dp0"
set "HFS=D:\DCC SoftWare\Houdini 21.0.440"
call tests\engine_loader.bat
if errorlevel 1 exit /b 1
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set "PATH=%HFS%\bin;%PATH%"
set "QT_QPA_PLATFORM_PLUGIN_PATH=%HFS%\bin\Qt_plugins\platforms"
cl /nologo /MD /O2 /W4 /WX /EHsc /std:c++17 /Zc:__cplusplus /permissive- /utf-8 /DWIN32 /DNOMINMAX /DQT_NO_KEYWORDS /external:I"%HFS%\toolkit\include" /external:W0 tests\cpp\display_text_layout_test.cpp /Fo:build\tests\display_text_layout_test.obj /Fe:build\tests\display_text_layout_test.exe /link /LIBPATH:"%HFS%\custom\houdini\dsolib" Qt6Core.lib Qt6Gui.lib
if errorlevel 1 exit /b 1
build\tests\display_text_layout_test.exe -platform offscreen
exit /b %errorlevel%
