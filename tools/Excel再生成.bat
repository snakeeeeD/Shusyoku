@echo off
chcp 65001 >nul
set PYTHONUTF8=1
cd /d "%~dp0"
echo Regenerating cards_edit.xlsx from cards.json ...
echo.
python cards_to_xlsx.py
echo.
echo Done. Press any key to close.
pause >nul
