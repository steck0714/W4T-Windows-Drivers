@echo off
setlocal
set "SRC=%~dp0"
set "DEST=%LOCALAPPDATA%\SwitchWOA\SwitchPowerTray"

if not exist "%SRC%SwitchPowerTray.exe" (
    echo ERROR: SwitchPowerTray.exe not found next to this script.
    exit /b 1
)

if exist "%DEST%\SwitchPowerTray.exe" (
    "%DEST%\SwitchPowerTray.exe" --restore-cursors
    if errorlevel 1 exit /b 1
    taskkill /IM SwitchPowerTray.exe /F >nul 2>nul
)

mkdir "%DEST%" 2>nul
copy /Y "%SRC%SwitchPowerTray.exe" "%DEST%\SwitchPowerTray.exe" >nul || exit /b 1
copy /Y "%SRC%Restore-SwitchPowerTray.cmd" "%DEST%\Restore-SwitchPowerTray.cmd" >nul || exit /b 1

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%SRC%Install-StartupTask.ps1" -App SwitchPowerTray -Executable "%DEST%\SwitchPowerTray.exe" || exit /b 1
start "" "%DEST%\SwitchPowerTray.exe" --startup-handheld --minimized

echo Installed SwitchPowerTray to "%DEST%" and enabled Start at Login.
echo Login defaults to Handheld once the GPU driver is ready. Charging limit is preserved.
exit /b 0
