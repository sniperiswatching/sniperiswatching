@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install_autostart.ps1"
if errorlevel 1 (
  echo 自动启动安装失败。
  pause
  exit /b 1
)
echo 已安装开机自动桥接。
pause
