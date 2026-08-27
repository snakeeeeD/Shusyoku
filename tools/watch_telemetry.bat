@echo off
chcp 65001 >nul
cd /d "%~dp0.."
echo テレメトリ自動更新を起動します（このウィンドウは開いたままにしてください）
python tools\watch_telemetry.py
pause
