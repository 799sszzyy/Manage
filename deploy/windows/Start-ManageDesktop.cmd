@echo off
setlocal
cd /d "%~dp0"
"bin\manage-desktop.exe" --api-url http://127.0.0.1:18080
if errorlevel 1 pause
