@echo off
rem SPDX-License-Identifier: MIT
rem Manual launch only: no startup-handheld or restore-defaults arguments.
setlocal
set "swoa_launch_failed="

if exist "%LOCALAPPDATA%\SwitchWOA\Pacecar\Pacecar.Switch.exe" (
    start "" /D "%LOCALAPPDATA%\SwitchWOA\Pacecar" "%LOCALAPPDATA%\SwitchWOA\Pacecar\Pacecar.Switch.exe"
    if errorlevel 1 set "swoa_launch_failed=1"
) else (
    echo Pacecar was not found in LocalAppData\SwitchWOA\Pacecar.
    set "swoa_launch_failed=1"
)

if exist "%LOCALAPPDATA%\SwitchWOA\SwitchPowerTray\SwitchPowerTray.exe" (
    start "" /D "%LOCALAPPDATA%\SwitchWOA\SwitchPowerTray" "%LOCALAPPDATA%\SwitchWOA\SwitchPowerTray\SwitchPowerTray.exe"
    if errorlevel 1 set "swoa_launch_failed=1"
) else (
    echo Switch Power was not found in LocalAppData\SwitchWOA\SwitchPowerTray.
    set "swoa_launch_failed=1"
)

if defined swoa_launch_failed (
    echo Unable to open one or more hardware tools.
    pause
    exit /b 1
)
exit /b 0
