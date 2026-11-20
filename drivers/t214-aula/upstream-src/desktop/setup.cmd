@echo off
rem Runs instead of oobe\windeploy.exe; copies setup logs to CRASHDUMP\agent (letter found by findagent.cmd).
for %%i in ("%~dp0.") do set "SA=%%~fi"
set "L=%SA%"
call "%SA%\findagent.cmd" w32.cmd
if defined AGENT set "L=%AGENT%:\agent"
echo ==== SETUP %date% %time% >> "%L%\w32-setup.txt"
reg query HKLM\SYSTEM\Setup >> "%L%\w32-setup.txt" 2>&1
start "" /b %SystemRoot%\System32\cmd.exe /c "%SA%\copylogs.cmd" %L%
%SystemRoot%\System32\oobe\windeploy.exe
echo windeploy exit %errorlevel% %time% >> "%L%\w32-setup.txt"
reg query HKLM\SYSTEM\Setup >> "%L%\w32-setup.txt" 2>&1
call "%SA%\copylogs.cmd" %L% once
