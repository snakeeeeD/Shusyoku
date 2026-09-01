@echo off
cd /d "%~dp0.."
echo Updating telemetry Excel...
python "%~dp0telemetry_to_excel.py"
if errorlevel 1 (
  echo.
  echo FAILED: if telemetry_report.xlsx is open in Excel, close it and run again.
  pause
  exit /b 1
)
start "" "%~dp0telemetry_report.xlsx"
