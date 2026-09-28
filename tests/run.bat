@echo off
setlocal
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "TESTOUT=%ROOT%\build\tests"
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
set "QT=%QT6_BASELINE%"
if not exist "%QT%\lib\Qt6Core.lib" set "QT=%ROOT%\..\_ThirdParty\Qt\6.8.3\msvc2022_64"
if not exist "%QT%\lib\Qt6Core.lib" set "QT=%ROOT%\third_party\qt6sdk"

if not exist "%VCVARS%" (
    echo [ERROR] vcvars64.bat not found.
    exit /b 1
)
if not exist "%QT%\lib\Qt6Core.lib" (
    echo [ERROR] Qt 6 x64 MSVC SDK not found.
    exit /b 1
)
if not exist "%TESTOUT%" mkdir "%TESTOUT%"
call "%VCVARS%" >nul
if not "%errorlevel%"=="0" exit /b 1
set "PATH=%QT%\bin;%PATH%"
set "PYTHONDONTWRITEBYTECODE=1"

cl /nologo /MD /O2 /W4 /WX /EHsc /std:c++17 /Zc:__cplusplus /permissive- /utf-8 ^
   /DQT_NO_KEYWORDS /I"%QT%\include" ^
   "%ROOT%\tests\cpp\matching_test.cpp" ^
   /Fo:"%TESTOUT%\matching_test.obj" /Fe:"%TESTOUT%\matching_test.exe" ^
   /link /LIBPATH:"%QT%\lib" Qt6Core.lib
if errorlevel 1 exit /b 1
"%TESTOUT%\matching_test.exe" "%ROOT%\translations\dictionary_zh.json" "%TESTOUT%\matching-conflicts.txt"
if errorlevel 1 exit /b 1

cl /nologo /MD /O2 /W4 /WX /EHsc /std:c++17 /utf-8 ^
   "%ROOT%\tests\cpp\translation_json_test.cpp" ^
   /Fo:"%TESTOUT%\translation_json_test.obj" ^
   /Fe:"%TESTOUT%\translation_json_test.exe"
if not "%errorlevel%"=="0" exit /b 1

set "ROUGH_DICTIONARY_ARG="
if exist "%ROOT%\translations\drafts\dictionary_rough_zh.json" set ROUGH_DICTIONARY_ARG="%ROOT%\translations\drafts\dictionary_rough_zh.json"
"%TESTOUT%\translation_json_test.exe" ^
   "%ROOT%\translations\dictionary_zh.json" ^
   %ROUGH_DICTIONARY_ARG% ^
   "%ROOT%\translations\templates_zh.json"
if not "%errorlevel%"=="0" exit /b 1

cl /nologo /MD /O2 /W4 /WX /EHsc /std:c++17 /Zc:__cplusplus /permissive- /utf-8 ^
   /DQT_NO_KEYWORDS ^
   /I"%QT%\include" ^
   "%ROOT%\tests\cpp\placeholder_templates_test.cpp" ^
   /Fo:"%TESTOUT%\placeholder_templates_test.obj" ^
   /Fe:"%TESTOUT%\placeholder_templates_test.exe" ^
   /link /LIBPATH:"%QT%\lib" Qt6Core.lib
if not "%errorlevel%"=="0" exit /b 1

"%TESTOUT%\placeholder_templates_test.exe" ^
   "%ROOT%\translations\templates_zh.json"
if not "%errorlevel%"=="0" exit /b 1

cl /nologo /MD /O2 /W4 /WX /EHsc /std:c++17 /Zc:__cplusplus /permissive- /utf-8 ^
   /DQT_NO_KEYWORDS ^
   /I"%QT%\include" ^
   "%ROOT%\tests\cpp\display_text_test.cpp" ^
   /Fo:"%TESTOUT%\display_text_test.obj" ^
   /Fe:"%TESTOUT%\display_text_test.exe" ^
   /link /LIBPATH:"%QT%\lib" Qt6Core.lib
if not "%errorlevel%"=="0" exit /b 1

"%TESTOUT%\display_text_test.exe"
if not "%errorlevel%"=="0" exit /b 1

cl /nologo /MD /O2 /W4 /WX /EHsc /std:c++17 /Zc:__cplusplus /permissive- /utf-8 ^
   /DQT_NO_KEYWORDS ^
   /I"%QT%\include" ^
   "%ROOT%\tests\cpp\translation_lookup_cache_test.cpp" ^
   /Fo:"%TESTOUT%\translation_lookup_cache_test.obj" ^
   /Fe:"%TESTOUT%\translation_lookup_cache_test.exe" ^
   /link /LIBPATH:"%QT%\lib" Qt6Core.lib
if not "%errorlevel%"=="0" exit /b 1

"%TESTOUT%\translation_lookup_cache_test.exe"
if not "%errorlevel%"=="0" exit /b 1

cl /nologo /MD /O2 /W4 /WX /EHsc /std:c++17 /utf-8 ^
   "%ROOT%\tests\cpp\runtime_compatibility_test.cpp" ^
   /Fo:"%TESTOUT%\runtime_compatibility_test.obj" ^
   /Fe:"%TESTOUT%\runtime_compatibility_test.exe"
if not "%errorlevel%"=="0" exit /b 1

"%TESTOUT%\runtime_compatibility_test.exe"
if not "%errorlevel%"=="0" exit /b 1

cl /nologo /MD /O2 /W4 /WX /EHsc /std:c++17 /Zc:__cplusplus /permissive- /utf-8 ^
   /DQT_NO_KEYWORDS /DQT_WIDGETS_LIB /DQT_GUI_LIB ^
   /I"%QT%\include" ^
   "%ROOT%\tests\cpp\plugin_config_test.cpp" ^
   /Fo:"%TESTOUT%\plugin_config_test.obj" ^
   /Fe:"%TESTOUT%\plugin_config_test.exe" ^
   /link /LIBPATH:"%QT%\lib" Qt6Core.lib Qt6Gui.lib Qt6Widgets.lib Qt6Test.lib
if not "%errorlevel%"=="0" exit /b 1

"%TESTOUT%\plugin_config_test.exe" -platform offscreen
if not "%errorlevel%"=="0" exit /b 1

cl /nologo /MD /O2 /W4 /WX /EHsc /std:c++17 /Zc:__cplusplus /permissive- /utf-8 ^
   /DWIN32 /D_WIN32_WINNT=0x0600 /DNOMINMAX /DSTRICT /DWIN32_LEAN_AND_MEAN ^
   /DQT_NO_KEYWORDS ^
   /I"%QT%\include" ^
   "%ROOT%\tests\cpp\untranslated_capture_test.cpp" ^
   /Fo:"%TESTOUT%\untranslated_capture_test.obj" ^
   /Fe:"%TESTOUT%\untranslated_capture_test.exe" ^
   /link /LIBPATH:"%QT%\lib" Qt6Core.lib ^
   user32.lib
if not "%errorlevel%"=="0" exit /b 1

"%TESTOUT%\untranslated_capture_test.exe"
if not "%errorlevel%"=="0" exit /b 1

cl /nologo /MD /O2 /W4 /WX /EHsc /std:c++17 /utf-8 ^
   "%ROOT%\tests\cpp\native_tree_capture_test.cpp" ^
   /Fo:"%TESTOUT%\native_tree_capture_test.obj" ^
   /Fe:"%TESTOUT%\native_tree_capture_test.exe"
if not "%errorlevel%"=="0" exit /b 1

"%TESTOUT%\native_tree_capture_test.exe"
if not "%errorlevel%"=="0" exit /b 1

cl /nologo /MD /O2 /W4 /WX /EHsc /std:c++17 /utf-8 ^
   "%ROOT%\tests\cpp\native_control_capture_test.cpp" ^
   /Fo:"%TESTOUT%\native_control_capture_test.obj" ^
   /Fe:"%TESTOUT%\native_control_capture_test.exe"
if not "%errorlevel%"=="0" exit /b 1

"%TESTOUT%\native_control_capture_test.exe"
if not "%errorlevel%"=="0" exit /b 1

cl /nologo /MD /O2 /W4 /WX /EHsc /GR /std:c++17 /utf-8 ^
   "%ROOT%\tests\cpp\native_rtti_test.cpp" ^
   /Fo:"%TESTOUT%\native_rtti_test.obj" ^
   /Fe:"%TESTOUT%\native_rtti_test.exe"
if not "%errorlevel%"=="0" exit /b 1

"%TESTOUT%\native_rtti_test.exe"
if not "%errorlevel%"=="0" exit /b 1

cl /nologo /MD /O2 /W4 /WX /EHsc /std:c++17 /utf-8 ^
   "%ROOT%\tests\cpp\quick_text_width_test.cpp" ^
   /Fo:"%TESTOUT%\quick_text_width_test.obj" ^
   /Fe:"%TESTOUT%\quick_text_width_test.exe"
if not "%errorlevel%"=="0" exit /b 1

"%TESTOUT%\quick_text_width_test.exe"
if not "%errorlevel%"=="0" exit /b 1

cl /nologo /MD /O2 /W4 /WX /EHsc /std:c++17 /Zc:__cplusplus /permissive- /utf-8 ^
   /DQT_NO_KEYWORDS ^
   /I"%QT%\include" ^
   "%ROOT%\tests\cpp\rich_text_translation_test.cpp" ^
   /Fo:"%TESTOUT%\rich_text_translation_test.obj" ^
   /Fe:"%TESTOUT%\rich_text_translation_test.exe" ^
   /link /LIBPATH:"%QT%\lib" Qt6Core.lib
if not "%errorlevel%"=="0" exit /b 1

"%TESTOUT%\rich_text_translation_test.exe"
if not "%errorlevel%"=="0" exit /b 1

cl /nologo /MD /O2 /W4 /WX /EHsc /std:c++17 /Zc:__cplusplus /permissive- /utf-8 ^
   /DWIN32 /D_WIN32_WINNT=0x0600 /DNOMINMAX /DSTRICT /DWIN32_LEAN_AND_MEAN ^
   /DQT_NO_KEYWORDS ^
   /I"%QT%\include" ^
   "%ROOT%\tests\cpp\utf8_text_test.cpp" ^
   /Fo:"%TESTOUT%\utf8_text_test.obj" ^
   /Fe:"%TESTOUT%\utf8_text_test.exe" ^
   /link /LIBPATH:"%QT%\lib" Qt6Core.lib ^
   user32.lib
if not "%errorlevel%"=="0" exit /b 1

"%TESTOUT%\utf8_text_test.exe"
if not "%errorlevel%"=="0" exit /b 1

call "%ROOT%\tests\detour_allocation.bat"
if not "%errorlevel%"=="0" exit /b 1
call "%ROOT%\tests\hooks.bat"
if not "%errorlevel%"=="0" exit /b 1
call "%ROOT%\tests\engine_loader.bat"
if not "%errorlevel%"=="0" exit /b 1

call "%ROOT%\tests\native_dispatch.bat"
if not "%errorlevel%"=="0" exit /b 1

call "%ROOT%\tests\module_scan.bat"
if not "%errorlevel%"=="0" exit /b 1
call "%ROOT%\tests\hook_registry.bat"
if not "%errorlevel%"=="0" exit /b 1
call "%ROOT%\tests\import_hooks.bat"
if not "%errorlevel%"=="0" exit /b 1
call "%ROOT%\tests\diagnostics.bat"
if not "%errorlevel%"=="0" exit /b 1
call "%ROOT%\tests\text_layout.bat"
if not "%errorlevel%"=="0" exit /b 1
call "%ROOT%\tests\display_hooks.bat"
if not "%errorlevel%"=="0" exit /b 1

python -m unittest discover -s "%ROOT%\tests\python" -p "test_*.py"
if not "%errorlevel%"=="0" exit /b 1

call "%ROOT%\tests\shortcuts.bat"
if not "%errorlevel%"=="0" exit /b 1

call "%ROOT%\tests\buttons.bat"
exit /b %errorlevel%
