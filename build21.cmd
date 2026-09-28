@echo off
setlocal EnableExtensions
cd /d "%~dp0"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "HFS=D:\DCC SoftWare\Houdini 21.0.440"
set "OUT=%~dp0build\houdini21"
set "OBJ=%OUT%\obj"
set "CPP=%~dp0source"
set "QTINC=%HFS%\toolkit\include"
set "QTLIB=%HFS%\custom\houdini\dsolib"
set "CL="
set "INCLUDE=%~dp0third_party\qt-6.5.3-headers;%INCLUDE%"
python tools\prepare21.py
if errorlevel 1 exit /b 1
rc /nologo /fo "%OBJ%\engine.res" "%OBJ%\engine.rc"
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /W4 /WX /EHsc /std:c++17 /utf-8 /LD /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_WIN32_WINNT=0x0602 "%CPP%\loader.cpp" /Fo"%OBJ%\loader.obj" /link /OUT:"%OUT%\package\HoudiniChinese\HoudiniChineseHook.dll" /IMPLIB:"%OBJ%\loader.lib" version.lib
if errorlevel 1 exit /b 1
cl /nologo /O2 /MD /W3 /c /EHsc /std:c++17 /Zc:gotoScope- /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /I"%CPP%\detours" /Fo"%OBJ%\detours\\" "%CPP%\detours\detours.cpp" "%CPP%\detours\disasm.cpp" "%CPP%\detours\modules.cpp"
if errorlevel 1 exit /b 1
lib /nologo /OUT:"%OBJ%\detours.lib" "%OBJ%\detours\*.obj"
if errorlevel 1 exit /b 1
cl /nologo /O2 /MD /W4 /WX /c /EHsc /std:c++17 /Zc:__cplusplus /permissive- /utf-8 /DAMD64 /DSIZEOF_VOID_P=8 /DSESI_LITTLE_ENDIAN /DSWAP_BITFIELDS /DWIN32 /D_WIN32_WINNT=0x0600 /DNOMINMAX /DSTRICT /DWIN32_LEAN_AND_MEAN /DQT_NO_KEYWORDS /D_CRT_SECURE_NO_DEPRECATE /D_CRT_NONSTDC_NO_DEPRECATE /D_SCL_SECURE_NO_WARNINGS /DHBOOST_ALL_NO_LIB /DHC_HOUDINI_21 /DUSE_QT6=1 /external:I"%QTINC%" /external:W0 /I"%CPP%\detours" /FI"%OBJ%\engine_build_identity.h" "%CPP%\hook.cpp" /Fo"%OBJ%\hook.obj"
if errorlevel 1 exit /b 1
link /nologo /DLL "%OBJ%\hook.obj" "%OBJ%\engine.res" /OUT:"%OUT%\package\HoudiniChinese\bin\HoudiniChineseEngine.dll" /IMPLIB:"%OBJ%\engine.lib" /LIBPATH:"%QTLIB%" "%OBJ%\detours.lib" libUT.lib Qt6Core.lib Qt6Gui.lib Qt6Widgets.lib Qt6Qml.lib Qt6Quick.lib user32.lib psapi.lib version.lib
exit /b %errorlevel%
