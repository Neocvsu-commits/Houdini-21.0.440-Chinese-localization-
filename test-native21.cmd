@echo off
setlocal
cd /d "%~dp0"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set "HFS=D:\DCC SoftWare\Houdini 21.0.440"
set "PATH=%HFS%\bin;%PATH%"
set "INCLUDE=%~dp0third_party\qt-6.5.3-headers;%INCLUDE%"
set "QT_QPA_PLATFORM_PLUGIN_PATH=%HFS%\bin\Qt_plugins\platforms"
cl /nologo /O2 /MD /W4 /WX /EHsc /std:c++17 /Zc:__cplusplus /permissive- /utf-8 /DAMD64 /DSIZEOF_VOID_P=8 /DSESI_LITTLE_ENDIAN /DSWAP_BITFIELDS /DWIN32 /D_WIN32_WINNT=0x0600 /DNOMINMAX /DSTRICT /DWIN32_LEAN_AND_MEAN /DQT_NO_KEYWORDS /D_CRT_SECURE_NO_DEPRECATE /D_CRT_NONSTDC_NO_DEPRECATE /D_SCL_SECURE_NO_WARNINGS /DHBOOST_ALL_NO_LIB /DHC_HOUDINI_21 /DUSE_QT6=1 /external:I"%HFS%\toolkit\include" /external:W0 /FI"%~dp0build\houdini21\obj\engine_build_identity.h" tests\cpp\native21_regression_test.cpp /Fo:build\tests\native21_regression_test.obj /Fe:build\tests\native21_regression_test.exe /link /LIBPATH:"%HFS%\custom\houdini\dsolib" build\houdini21\obj\detours.lib libUT.lib Qt6Core.lib Qt6Gui.lib Qt6Widgets.lib Qt6Qml.lib Qt6Quick.lib user32.lib psapi.lib version.lib
if errorlevel 1 exit /b 1
build\tests\native21_regression_test.exe -platform offscreen
exit /b %errorlevel%
