@echo off
setlocal
echo Starting MiniUnix Shell Lab in WSL...
echo The browser will open when the local server is ready.
start "" powershell.exe -NoProfile -WindowStyle Hidden -File "%~dp0webapp\open_when_ready.ps1"
wsl --cd "%~dp0" -- python3 webapp/server.py
if errorlevel 1 (
  echo.
  echo The app could not start. Check that WSL Ubuntu, GCC, Make, and Python 3 are installed.
  pause
)
