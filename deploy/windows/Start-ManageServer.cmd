@echo off
setlocal
cd /d "%~dp0"
echo Manage server will run in this visible window. Press Ctrl+C to stop it.
echo MySQL password input is hidden: type the password normally, then press Enter.
echo No characters or asterisks will appear while typing; this is normal.
"bin\manage-server.exe" --db-host 127.0.0.1 --db-port 3306 --db-name manage --db-user manage_app --prompt-db-password --listen-address 127.0.0.1 --port 18080
if errorlevel 1 pause
