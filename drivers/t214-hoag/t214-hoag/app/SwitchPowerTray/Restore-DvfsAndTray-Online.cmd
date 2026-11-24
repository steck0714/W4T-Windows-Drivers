@echo off
setlocal EnableExtensions
rem Run from the bundle root as administrator. This resets settings, not driver binaries.
rem GPU KMD/UMD/ICD package rollback remains with the host's existing recovery workflow.
set "HERE=%~dp0"
reg add "HKLM\SYSTEM\CurrentControlSet\Services\tegragpurender\Parameters" /v GpuDvfsMode /t REG_DWORD /d 1 /f >nul
if errorlevel 1 exit /b 1
reg add "HKLM\SYSTEM\CurrentControlSet\Services\tegragpurender\Parameters" /v GpuDvfsMinKhz /t REG_DWORD /d 76800 /f >nul
if errorlevel 1 exit /b 1
reg add "HKLM\SYSTEM\CurrentControlSet\Services\tegragpurender\Parameters" /v GpuDvfsMaxKhz /t REG_DWORD /d 537600 /f >nul
if errorlevel 1 exit /b 1
reg add "HKLM\SYSTEM\CurrentControlSet\Services\switchbattery\Parameters" /v ChargeLimit /t REG_DWORD /d 85 /f >nul
if errorlevel 1 exit /b 1
if not exist "%HERE%SwitchPowerTray\Restore-SwitchPowerTray.cmd" (
    echo ERROR: Restore-SwitchPowerTray.cmd is missing.
    exit /b 2
)
call "%HERE%SwitchPowerTray\Restore-SwitchPowerTray.cmd"
if errorlevel 1 exit /b 1
echo Handheld policy and the 85%% charging band requested. Verify actual GPU clock readback.
exit /b 0
