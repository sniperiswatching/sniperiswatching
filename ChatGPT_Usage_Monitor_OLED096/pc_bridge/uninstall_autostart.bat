@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0uninstall_autostart.ps1"
if errorlevel 1 (
  echo 自动启动卸载失败。
  pause
  exit /b 1
)
echo 已取消开机自动桥接。
pause
