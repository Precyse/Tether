@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0warriors-setup.ps1" %*
exit /b %errorlevel%
