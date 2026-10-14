@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\BuildWindows.ps1" -Backend CUDA -Interactive
set "cvl_exit=%errorlevel%"
pause
exit /b %cvl_exit%
