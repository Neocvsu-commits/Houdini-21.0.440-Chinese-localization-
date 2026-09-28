@echo off
setlocal
cd /d "%~dp0"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set "HFS=D:\DCC SoftWare\Houdini 21.0.440"
cl /nologo /O2 /MD /W4 /WX /EHsc /std:c++17 /utf-8 /LD /DAMD64 /DSIZEOF_VOID_P=8 /DSESI_LITTLE_ENDIAN /DSWAP_BITFIELDS /DWIN32 /D_WIN32_WINNT=0x0600 /DNOMINMAX /DSTRICT /DWIN32_LEAN_AND_MEAN /DHBOOST_ALL_NO_LIB /external:I"%HFS%\toolkit\include" /external:W0 tests\cpp\help_probe21.cpp /Fo:build\tests\help_probe21.obj /link /OUT:build\tests\help_probe21.dll /LIBPATH:"%HFS%\custom\houdini\dsolib" libUT.lib
exit /b %errorlevel%
