@echo off
net session >nul 2>&1 || (echo Run this as Administrator. & pause & exit /b 1)
echo Stopping (this can take a minute)...
netsh trace stop
echo.
dir %SystemDrive%\SwitchWOA-wlan.etl
echo Copy %SystemDrive%\SwitchWOA-wlan.etl to the USB drive and bring it to the PC.
pause
