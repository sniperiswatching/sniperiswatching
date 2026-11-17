@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0uninstall_autostart.ps1"
if errorlevel 1 (
  echo Autostart removal failed.
  pause
  exit /b 1
)
echo Autostart bridge removed.
pause
