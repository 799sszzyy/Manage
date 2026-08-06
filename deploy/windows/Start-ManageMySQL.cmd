@echo off
setlocal
set "MYSQL_HOME=C:\Program Files\MySQL\MySQL Server 8.4"
set "MYSQL_CONFIG=C:\ProgramData\ManageMySQL84\my.ini"
if not exist "%MYSQL_HOME%\bin\mysqld.exe" (
    echo MySQL 8.4 was not found at "%MYSQL_HOME%".
    pause
    exit /b 1
)
if not exist "%MYSQL_CONFIG%" (
    echo ManageMySQL84 has not been initialized yet: "%MYSQL_CONFIG%"
    pause
    exit /b 1
)
tasklist /FI "IMAGENAME eq mysqld.exe" | find /I "mysqld.exe" >nul
if not errorlevel 1 (
    echo MySQL is already running.
    exit /b 0
)
start "ManageMySQL84" /min "%MYSQL_HOME%\bin\mysqld.exe" --defaults-file="%MYSQL_CONFIG%"
echo MySQL started on 127.0.0.1:3306.
