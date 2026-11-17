@echo off
setlocal
cd /d "%~dp0"
where py >nul 2>nul
if %errorlevel%==0 (
  py bridge.py
  exit /b %errorlevel%
)
where python >nul 2>nul
if %errorlevel%==0 (
  python bridge.py
  exit /b %errorlevel%
)
echo 未找到 Python 3，请先安装 Python 3。
pause

