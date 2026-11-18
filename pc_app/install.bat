@echo off
where python >nul 2>nul || (echo Python not found. Install from python.org & pause & exit /b 1)
python -m venv .venv
call .venv\Scripts\activate.bat
python -m pip install --upgrade pip
pip install -r requirements.txt
echo Done. Run start_radar.bat
pause
