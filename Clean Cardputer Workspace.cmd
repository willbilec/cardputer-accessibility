@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0builder\clean_workspace.ps1" -Interactive
set "cleanupExitCode=%errorlevel%"
echo.
pause
exit /b %cleanupExitCode%
