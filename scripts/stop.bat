@echo off
REM Stops the NetScan background server started by NetScan.exe.

setlocal

set "SERVER_EXE=netscan-server.exe"
set "PID_FILE=%LOCALAPPDATA%\NetScan\netscan.pid"

set "SERVER_PID="
if exist "%PID_FILE%" (
    set /p SERVER_PID=<"%PID_FILE%"
    for /f "delims=0123456789" %%A in ("%SERVER_PID%") do set "SERVER_PID="
)

if defined SERVER_PID (
    taskkill /F /PID %SERVER_PID% /T >nul 2>&1
    if %ERRORLEVEL% equ 0 (
        echo Stopped NetScan server PID %SERVER_PID%.
    ) else (
        taskkill /F /IM %SERVER_EXE% /T >nul 2>&1
        if %ERRORLEVEL% equ 0 (
            echo Stopped NetScan server by image name.
        ) else (
            echo No running NetScan server process found.
        )
    )
) else (
    taskkill /F /IM %SERVER_EXE% /T >nul 2>&1
    if %ERRORLEVEL% equ 0 (
        echo Stopped NetScan server by image name.
    ) else (
        echo No running NetScan server process found.
    )
)

if exist "%PID_FILE%" del /F /Q "%PID_FILE%" >nul 2>&1

echo Done.
endlocal
