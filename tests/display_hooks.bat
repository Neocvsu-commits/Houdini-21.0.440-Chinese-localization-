@echo off
setlocal
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
if not defined HFS set "HFS=C:\Program Files\Side Effects Software\Houdini 22.0.368"
set "QT=%QT6_BASELINE%"
if not exist "%QT%\lib\Qt6Quick.lib" set "QT=%ROOT%\..\_ThirdParty\Qt\6.8.3\msvc2022_64"
if not exist "%QT%\lib\Qt6Quick.lib" exit /b 1
if not defined HOUDINI_CHINESE_TEST_BUILD set "HOUDINI_CHINESE_TEST_BUILD=%ROOT%\build\display-hook-test-build"
if not exist "%HOUDINI_CHINESE_TEST_BUILD%\obj\detours.lib" (
    set "HOUDINI_CHINESE_OUT=%HOUDINI_CHINESE_TEST_BUILD%"
    call "%ROOT%\tools\build.bat"
    if errorlevel 1 exit /b 1
)
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist "%ROOT%\build\tests" mkdir "%ROOT%\build\tests"
set "PATH=%QT%\bin;%HFS%\bin;%PATH%"
set "QT_PLUGIN_PATH=%QT%\plugins"
set "QT_QPA_PLATFORM_PLUGIN_PATH=%QT%\plugins\platforms"
set "QML_IMPORT_PATH=%QT%\qml"
cl /nologo /O2 /MD /W4 /WX /EHsc /std:c++20 /Zc:__cplusplus /permissive- /utf-8 ^
   /DAMD64 /DSIZEOF_VOID_P=8 /DSESI_LITTLE_ENDIAN /DSWAP_BITFIELDS ^
   /DWIN32 /D_WIN32_WINNT=0x0600 /DNOMINMAX /DSTRICT /DWIN32_LEAN_AND_MEAN ^
   /DQT_NO_KEYWORDS /D_CRT_SECURE_NO_DEPRECATE /D_CRT_NONSTDC_NO_DEPRECATE ^
   /D_SCL_SECURE_NO_WARNINGS /DHBOOST_ALL_NO_LIB /DUSE_QT6=1 ^
   /I"%QT%\include" /external:I"%HFS%\toolkit\include" /external:W0 ^
   "%ROOT%\tests\cpp\display_hooks_test.cpp" ^
   /Fo:"%ROOT%\build\tests\display_hooks_test.obj" ^
   /Fe:"%ROOT%\build\tests\display_hooks_test.exe" ^
   /link /LIBPATH:"%HFS%\custom\houdini\dsolib" /LIBPATH:"%QT%\lib" ^
   "%HOUDINI_CHINESE_TEST_BUILD%\obj\detours.lib" libUT.lib ^
   Qt6Core.lib Qt6Gui.lib Qt6Widgets.lib Qt6Qml.lib Qt6Quick.lib ^
   user32.lib psapi.lib version.lib
if errorlevel 1 exit /b 1
"%ROOT%\build\tests\display_hooks_test.exe" -platform offscreen
exit /b %errorlevel%
