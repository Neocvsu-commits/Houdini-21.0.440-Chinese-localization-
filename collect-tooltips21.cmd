@echo off
rem Collect tooltips: dump shelf help text, then re-run coverage audit.
rem Keep this file ASCII-only: cmd.exe reads it with the OEM codepage.
setlocal EnableExtensions
cd /d "%~dp0"

set "HY=D:\DCC SoftWare\Houdini 21.0.440\bin\hython.exe"
if not exist "%HY%" set "HY=hython.exe"
if "%HC_TOOLTIP_BUDGET%"=="" set "HC_TOOLTIP_BUDGET=600"

echo [1/3] Dumping shelf tooltips (1-3 minutes, HC_TOOLTIP_BUDGET=%HC_TOOLTIP_BUDGET%s)
"%HY%" tools\dump_shelf_tooltips21.py
if errorlevel 1 echo   WARNING: dump failed, continuing with audit.

echo.
echo [2/3] Auditing coverage
python tools\audit_ui_strings21.py
if errorlevel 1 goto :fail

echo.
echo [3/3] Summary
python -c "import json,io;d=json.load(io.open(r'build/ui_audit/summary.json',encoding='utf-8'));print('ui_coverage', d['ui_coverage']);print('ui_uncovered', d['ui_uncovered']);print('help_cache_missing', d['sources'].get('help_cache',{}).get('missing'))"
echo.
echo Uncovered list: build\ui_audit\uncovered.tsv
echo Send that file (or its help_cache rows) back for translation.
exit /b 0

:fail
echo Audit failed. Please send the error above.
exit /b 1
