@echo off
chcp 65001 >nul
set PYTHONUTF8=1
cd /d "%~dp0"
echo Generating GameContent.xlsx from JSON ...
echo.
python gen_content_sheet.py
echo.
echo Done. Press any key to close.
pause >nul
