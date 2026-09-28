@echo off
setlocal EnableExtensions
rem ===========================================================================
rem Houdini 中文汉化 - version-aware Windows build
rem
rem Optional environment variables:
rem   HFS                       Target Houdini installation.
rem   QT6_BASELINE              Matching Qt 6 x64 MSVC SDK.
rem   HOUDINI_CHINESE_OUT       Output directory (default: build\out).
rem   HOUDINI_CHINESE_INCLUDE_DRAFTS=1   Stage the rough dictionary.
rem ===========================================================================

for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "CPP=%ROOT%\source"
if defined HOUDINI_CHINESE_OUT (
    set "OUT=%HOUDINI_CHINESE_OUT%"
) else (
    set "OUT=%ROOT%\build\out"
)
for %%I in ("%OUT%") do set "OUT=%%~fI"
set "OBJDIR=%OUT%\obj"

rem Prefer an explicitly selected HDK. Otherwise use an installed Houdini that
rem contains the required headers. The matrix script performs ordered discovery
rem when more than one version is installed.
if not defined HFS (
    for /d %%D in ("C:\Program Files\Side Effects Software\Houdini *") do (
        if exist "%%~fD\toolkit\include\SYS\SYS_Version.h" if exist "%%~fD\bin\Qt6Core.dll" set "HFS=%%~fD"
    )
)

if not defined HFS (
    echo [ERROR] No Houdini HDK installation was found. Set HFS explicitly.
    exit /b 1
)
if not exist "%HFS%\toolkit\include\SYS\SYS_Version.h" (
    echo [ERROR] Invalid HFS: %HFS%
    exit /b 1
)
if not exist "%HFS%\custom\houdini\dsolib\libUT.lib" (
    echo [ERROR] libUT.lib was not found under the selected HFS.
    exit /b 1
)
if not exist "%HFS%\bin\Qt6Core.dll" (
    echo [ERROR] The selected Houdini is not a Qt 6 build: %HFS%
    exit /b 1
)

set "HVER="
set "HFULL="
set "HMAJOR="
for /f "tokens=3" %%V in ('findstr /b /c:"#define SYS_VERSION_FULL " "%HFS%\toolkit\include\SYS\SYS_Version.h"') do set "HFULL=%%~V"
for /f "tokens=3" %%V in ('findstr /b /c:"#define SYS_VERSION_RELEASE " "%HFS%\toolkit\include\SYS\SYS_Version.h"') do set "HVER=%%~V"
for /f "tokens=3" %%V in ('findstr /b /c:"#define SYS_VERSION_MAJOR_INT " "%HFS%\toolkit\include\SYS\SYS_Version.h"') do set "HMAJOR=%%~V"
if not defined HVER (
    echo [ERROR] Could not determine the Houdini release from SYS_Version.h.
    exit /b 1
)
if not defined HMAJOR (
    echo [ERROR] Could not determine the Houdini major version from SYS_Version.h.
    exit /b 1
)
set "QTVERSION="
for /f "usebackq delims=" %%V in (`powershell -NoProfile -Command "$v = [version](Get-Item -LiteralPath (Join-Path $env:HFS 'bin\Qt6Core.dll')).VersionInfo.FileVersion; '{0}.{1}.{2}' -f $v.Major,$v.Minor,$v.Build"`) do set "QTVERSION=%%V"
if not defined QTVERSION (
    echo [ERROR] Could not determine the Houdini Qt runtime version.
    exit /b 1
)
if defined QT6_BASELINE if not exist "%QT6_BASELINE%\lib\Qt6Core.lib" (
    echo [ERROR] Explicit QT6_BASELINE is not a Qt SDK.
    exit /b 1
)
set "QT=%QT6_BASELINE%"
if not exist "%QT%\lib\Qt6Core.lib" if exist "%ROOT%\..\_ThirdParty\Qt\%QTVERSION%\msvc2022_64\lib\Qt6Core.lib" set "QT=%ROOT%\..\_ThirdParty\Qt\%QTVERSION%\msvc2022_64"
if not exist "%QT%\lib\Qt6Core.lib" for /d %%Q in ("%ROOT%\..\_ThirdParty\Qt\%QTVERSION%\msvc*_64") do if exist "%%~fQ\lib\Qt6Core.lib" set "QT=%%~fQ"
if not exist "%QT%\lib\Qt6Core.lib" if exist "%ROOT%\third_party\qt6sdk\lib\Qt6Core.lib" set "QT=%ROOT%\third_party\qt6sdk"
if not exist "%QT%\lib\Qt6Core.lib" (
    echo [ERROR] Matching Qt %QTVERSION% x64 MSVC SDK not found.
    echo Set QT6_BASELINE to the SDK used by Houdini %HVER%.
    exit /b 1
)

set "QTSDKVERSION="
for /f "usebackq delims=" %%V in (`powershell -NoProfile -Command "$v = [version](Get-Item -LiteralPath (Join-Path $env:QT 'bin\Qt6Core.dll')).VersionInfo.FileVersion; '{0}.{1}.{2}' -f $v.Major,$v.Minor,$v.Build"`) do set "QTSDKVERSION=%%V"
if /I not "%QTSDKVERSION%"=="%QTVERSION%" (
    echo [ERROR] Qt SDK %QTSDKVERSION% does not match Houdini Qt runtime %QTVERSION%.
    exit /b 1
)

set "CPPSTD=c++17"
if %HMAJOR% GEQ 22 set "CPPSTD=c++20"

if not exist "%ROOT%\package\HoudiniChinese.template.json" (
    echo [ERROR] Missing package template.
    exit /b 1
)
if not exist "%ROOT%\package\HoudiniChinese\MainMenuCommon.xml" (
    echo [ERROR] Missing MainMenuCommon.xml.
    exit /b 1
)
if not exist "%ROOT%\translations\dictionary_zh.json" (
    echo [ERROR] Missing formal dictionary_zh.json.
    exit /b 1
)
if not exist "%ROOT%\translations\templates_zh.json" (
    echo [ERROR] Missing templates_zh.json.
    exit /b 1
)

set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo [ERROR] vcvars64.bat not found. Install VS2022 Build Tools.
    exit /b 1
)
call "%VCVARS%" >nul
if errorlevel 1 exit /b 1

rem Cleanup is performed in one shell after validating resolved paths, ownership
rem and every descendant for links/junctions. Existing unmarked data is refused.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare_build_output.ps1" -ProjectRoot "%ROOT%" -OutputDirectory "%OUT%"
if errorlevel 1 exit /b 1

echo === Target: Houdini %HVER%, Qt %QTVERSION%, %CPPSTD% ===
python "%ROOT%\tools\generate_engine_metadata.py" "%HFS%" "%QTVERSION%" "%OBJDIR%"
if errorlevel 1 exit /b 1
set /p ENGINE_NAME=<"%OBJDIR%\engine_name.txt"
rc /nologo /fo "%OBJDIR%\engine.res" "%OBJDIR%\engine.rc"
if errorlevel 1 exit /b 1

rem The loader shares no allocations with Qt/HDK and has no dependency on them.
cl /nologo /O2 /MT /W4 /WX /EHsc /std:c++17 /utf-8 /LD ^
   /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_WIN32_WINNT=0x0602 ^
   "%CPP%\loader.cpp" /Fo"%OBJDIR%\loader.obj" ^
   /link /OUT:"%OUT%\HoudiniChineseHook.dll" ^
   /IMPLIB:"%OBJDIR%\loader.lib" version.lib
if errorlevel 1 exit /b 1

echo === Compiling Detours ===
cl /nologo /O2 /MD /W3 /c /EHsc /std:c++17 /Zc:gotoScope- ^
   /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS ^
   /I"%CPP%\detours" /Fo"%OBJDIR%\detours\\" ^
   "%CPP%\detours\detours.cpp" "%CPP%\detours\disasm.cpp" ^
   "%CPP%\detours\modules.cpp"
if errorlevel 1 exit /b 1
lib /nologo /OUT:"%OBJDIR%\detours.lib" "%OBJDIR%\detours\*.obj"
if errorlevel 1 exit /b 1

echo === Compiling hook DLL (Qt 6) ===
cl /nologo /O2 /MD /W4 /WX /c /EHsc /std:%CPPSTD% /Zc:__cplusplus /permissive- /utf-8 ^
   /DAMD64 /DSIZEOF_VOID_P=8 /DSESI_LITTLE_ENDIAN /DSWAP_BITFIELDS ^
   /DWIN32 /D_WIN32_WINNT=0x0600 /DNOMINMAX /DSTRICT /DWIN32_LEAN_AND_MEAN ^
   /DQT_NO_KEYWORDS /D_CRT_SECURE_NO_DEPRECATE /D_CRT_NONSTDC_NO_DEPRECATE ^
   /D_SCL_SECURE_NO_WARNINGS /DHBOOST_ALL_NO_LIB /DUSE_QT6=1 ^
   /I"%QT%\include" ^
   /external:I"%HFS%\toolkit\include" /external:W0 ^
   /I"%CPP%\detours" ^
   /FI"%OBJDIR%\engine_build_identity.h" ^
   "%CPP%\hook.cpp" ^
   /Fo"%OBJDIR%\hook.obj"
if errorlevel 1 exit /b 1

rem Qt and the HDK use the dynamic MSVC runtime.  Inline Qt containers may be
rem allocated inside Qt and released inside this DLL, so /MT would corrupt the
rem host heap when those module boundaries are crossed.
dumpbin /directives "%OBJDIR%\hook.obj" | findstr /c:"/DEFAULTLIB:MSVCRT" >nul
if errorlevel 1 (
    echo [ERROR] hook.obj was not built with the dynamic MSVC runtime.
    exit /b 1
)
dumpbin /directives "%OBJDIR%\detours\detours.obj" | findstr /c:"/DEFAULTLIB:MSVCRT" >nul
if errorlevel 1 (
    echo [ERROR] Detours was not built with the dynamic MSVC runtime.
    exit /b 1
)

link /nologo /DLL "%OBJDIR%\hook.obj" "%OBJDIR%\engine.res" ^
   /OUT:"%OBJDIR%\%ENGINE_NAME%" ^
   /IMPLIB:"%OBJDIR%\HoudiniChineseHook.lib" ^
   /LIBPATH:"%HFS%\custom\houdini\dsolib" ^
   "%OBJDIR%\detours.lib" ^
   libUT.lib ^
   "%QT%\lib\Qt6Core.lib" "%QT%\lib\Qt6Gui.lib" "%QT%\lib\Qt6Widgets.lib" "%QT%\lib\Qt6Qml.lib" "%QT%\lib\Qt6Quick.lib" ^
   user32.lib psapi.lib version.lib
if errorlevel 1 exit /b 1

echo === Staging Houdini %HVER% package ===
if not exist "%OUT%\package\HoudiniChinese\translations" mkdir "%OUT%\package\HoudiniChinese\translations"
if errorlevel 1 exit /b 1
python "%ROOT%\tools\generate_package_json.py" ^
   "%ROOT%\package\HoudiniChinese.template.json" ^
   "%OUT%\package\HoudiniChinese.json" "%HVER%"
if errorlevel 1 exit /b 1
copy /Y "%OUT%\HoudiniChineseHook.dll" "%OUT%\package\HoudiniChinese\HoudiniChineseHook.dll" >nul
if errorlevel 1 exit /b 1
copy /Y "%OBJDIR%\%ENGINE_NAME%" "%OUT%\package\HoudiniChinese\%ENGINE_NAME%" >nul
if errorlevel 1 exit /b 1
copy /Y "%OBJDIR%\engine.json" "%OUT%\package\HoudiniChinese\engine.json" >nul
if errorlevel 1 exit /b 1
copy /Y "%ROOT%\package\HoudiniChinese\MainMenuCommon.xml" "%OUT%\package\HoudiniChinese\MainMenuCommon.xml" >nul
if errorlevel 1 exit /b 1
copy /Y "%ROOT%\translations\dictionary_zh.json" "%OUT%\package\HoudiniChinese\translations\dictionary_zh.json" >nul
if errorlevel 1 exit /b 1
if /I "%HOUDINI_CHINESE_INCLUDE_DRAFTS%"=="1" if not exist "%ROOT%\translations\drafts\dictionary_rough_zh.json" (
    echo [ERROR] Draft inclusion was requested, but dictionary_rough_zh.json is missing.
    exit /b 1
)
if /I "%HOUDINI_CHINESE_INCLUDE_DRAFTS%"=="1" if exist "%ROOT%\translations\drafts\dictionary_rough_zh.json" (
    copy /Y "%ROOT%\translations\drafts\dictionary_rough_zh.json" "%OUT%\package\HoudiniChinese\translations\dictionary_rough_zh.json" >nul
    if errorlevel 1 exit /b 1
)
copy /Y "%ROOT%\translations\templates_zh.json" "%OUT%\package\HoudiniChinese\translations\templates_zh.json" >nul
if errorlevel 1 exit /b 1

rem Use the target host's Python ABI directories, not the build-shell Python.
set "HC_PYTHON_STAGED="
for /d %%P in ("%HFS%\houdini\python*libs") do (
    if not exist "%OUT%\package\HoudiniChinese\%%~nxP" mkdir "%OUT%\package\HoudiniChinese\%%~nxP"
    for %%S in (pythonrc.py uiready.py houdini_chinese_startup.py) do (
        copy /Y "%ROOT%\package\HoudiniChinese\%%S" "%OUT%\package\HoudiniChinese\%%~nxP\%%S" >nul
        if errorlevel 1 exit /b 1
    )
    set "HC_PYTHON_STAGED=1"
)
if not defined HC_PYTHON_STAGED (
    echo [ERROR] Could not find the target Houdini Python startup directory.
    exit /b 1
)

echo Build complete for Houdini %HVER%:
python "%ROOT%\tools\create_dist.py" "%OUT%\package" "%ROOT%\dist" "%HFULL%" --dictionary-source "%ROOT%\translations"
if errorlevel 1 exit /b 1
echo   %OUT%\HoudiniChineseHook.dll
echo   %OUT%\package\HoudiniChinese.json
endlocal
