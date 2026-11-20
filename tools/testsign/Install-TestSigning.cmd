@echo off
rem Run on the Switch's Windows as Administrator (right click > Run as administrator).
rem Trusts SwitchWOA-Test.cer (Root + TrustedPublisher) and turns test signing on, then reboot.
rem Same for every model (HAC-001 / HAC-001-01 / HDH-001 / HEG-001). Log: %SystemDrive%\SwitchWOA-Stage0.txt
setlocal
set "PS=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%PS%" set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
"%PS%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0Stage0-SwitchSetup.ps1" -Mode Prepare
echo.
echo Reboot now, then run again with the argument "status" to check.
if /i "%~1"=="status" "%PS%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0Stage0-SwitchSetup.ps1" -Mode Status
pause
