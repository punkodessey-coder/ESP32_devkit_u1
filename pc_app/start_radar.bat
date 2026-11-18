@echo off
cd /d "%~dp0"
call .venv\Scripts\activate.bat
python radar_app.py
pause
