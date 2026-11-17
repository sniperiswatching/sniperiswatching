@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install_autostart.ps1"
if errorlevel 1 (
  echo Autostart installation failed.
  pause
  exit /b 1
)
echo Autostart bridge installed.
pause
