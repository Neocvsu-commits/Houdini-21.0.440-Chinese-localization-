@echo off
setlocal
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "CPP=%ROOT%\source"
if not defined HFS (
    for /d %%D in ("C:\Program Files\Side Effects Software\Houdini *") do (
        if exist "%%~fD\toolkit\include\SYS\SYS_Version.h" if exist "%%~fD\bin\Qt6Core.dll" set "HFS=%%~fD"
    )
)
if not defined HFS exit /b 1
if not exist "%HFS%\bin\Qt6Core.dll" exit /b 1
set "QTVERSION="
for /f "usebackq delims=" %%V in (`powershell -NoProfile -Command "$v = [version](Get-Item -LiteralPath (Join-Path $env:HFS 'bin\Qt6Core.dll')).VersionInfo.FileVersion; '{0}.{1}.{2}' -f $v.Major,$v.Minor,$v.Build"`) do set "QTVERSION=%%V"
if not defined QTVERSION exit /b 1
if defined QT6_BASELINE if not exist "%QT6_BASELINE%\lib\Qt6Core.lib" exit /b 1
set "QT=%QT6_BASELINE%"
if not exist "%QT%\lib\Qt6Core.lib" if exist "%ROOT%\..\_ThirdParty\Qt\%QTVERSION%\msvc2022_64\lib\Qt6Core.lib" set "QT=%ROOT%\..\_ThirdParty\Qt\%QTVERSION%\msvc2022_64"
if not exist "%QT%\lib\Qt6Core.lib" for /d %%Q in ("%ROOT%\..\_ThirdParty\Qt\%QTVERSION%\msvc*_64") do if exist "%%~fQ\lib\Qt6Core.lib" set "QT=%%~fQ"
if not exist "%QT%\lib\Qt6Core.lib" set "QT=%ROOT%\third_party\qt6sdk"
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

if not exist "%VCVARS%" exit /b 1
if not exist "%QT%\lib\Qt6Core.lib" exit /b 1
set "QTSDKVERSION="
for /f "usebackq delims=" %%V in (`powershell -NoProfile -Command "$v = [version](Get-Item -LiteralPath (Join-Path $env:QT 'bin\Qt6Core.dll')).VersionInfo.FileVersion; '{0}.{1}.{2}' -f $v.Major,$v.Minor,$v.Build"`) do set "QTSDKVERSION=%%V"
if /I not "%QTSDKVERSION%"=="%QTVERSION%" (
    echo [ERROR] Qt SDK does not match the target Houdini Qt runtime.
    exit /b 1
)
set "HMAJOR="
for /f "tokens=3" %%V in ('findstr /b /c:"#define SYS_VERSION_MAJOR_INT " "%HFS%\toolkit\include\SYS\SYS_Version.h"') do set "HMAJOR=%%~V"
if not defined HMAJOR exit /b 1
set "CPPSTD=c++17"
if %HMAJOR% GEQ 22 set "CPPSTD=c++20"
call "%VCVARS%" >nul
if errorlevel 1 exit /b 1
if not exist "%ROOT%\build" mkdir "%ROOT%\build"

cl /nologo /MD /analyze /analyze:only ^
   /analyze:log "%ROOT%\build\hook.nativecodeanalysis.xml" ^
   /W4 /EHsc /std:%CPPSTD% ^
   /Zc:__cplusplus /permissive- /utf-8 ^
   /DAMD64 /DSIZEOF_VOID_P=8 /DSESI_LITTLE_ENDIAN /DSWAP_BITFIELDS ^
   /DWIN32 /D_WIN32_WINNT=0x0600 /DNOMINMAX /DSTRICT /DWIN32_LEAN_AND_MEAN ^
   /DQT_NO_KEYWORDS /D_CRT_SECURE_NO_DEPRECATE /D_CRT_NONSTDC_NO_DEPRECATE ^
   /D_SCL_SECURE_NO_WARNINGS /DHBOOST_ALL_NO_LIB /DUSE_QT6=1 ^
   /external:I"%QT%\include" /external:W0 ^
   /external:I"%HFS%\toolkit\include" /external:W0 ^
   /I"%CPP%\detours" ^
   "%CPP%\hook.cpp"
exit /b %errorlevel%
