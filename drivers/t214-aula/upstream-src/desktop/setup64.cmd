@echo off
rem ARM64: runs instead of oobe\windeploy.exe; copies setup logs to \agent\a64 on the boot partition
rem (found at run time by findagent.cmd: any letter, or none).
for %%i in ("%~dp0.") do set "SA=%%~fi"
set "L=%SA%"
call "%SA%\findagent.cmd" w64.cmd
if defined AGENT set "L=%AGENT%:\agent\a64"
mkdir "%L%" 2>nul
echo ==== SETUP %date% %time% >> "%L%\setup.txt"
reg query HKLM\SYSTEM\Setup >> "%L%\setup.txt" 2>&1
start "" /b %SystemRoot%\System32\cmd.exe /c "%SA%\copylogs.cmd" %L%
start "" /b %SystemRoot%\System32\cmd.exe /c "%SA%\setupwatch.cmd" %L%
%SystemRoot%\System32\oobe\windeploy.exe
echo windeploy exit %errorlevel% %time% >> "%L%\setup.txt"
reg query HKLM\SYSTEM\Setup >> "%L%\setup.txt" 2>&1
call "%SA%\copylogs.cmd" %L% once
