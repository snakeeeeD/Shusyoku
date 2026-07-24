@echo off
chcp 65001 >nul
set PYTHONUTF8=1
cd /d "%~dp0"
echo Applying cards_edit.xlsx to cards.json ...
echo.
python xlsx_to_cards.py
echo.
echo Done. Press any key to close.
pause >nul
