@echo off
rem Started by the swagent service on every boot (Windows 10 ARM32).
rem The folder of this script (<system drive>:\swagent) holds the other agent files; the boot partition with
rem \agent\w32.cmd (label CRASHDUMP; its letter is not fixed and it may have none) is found by findagent.cmd.
set "SA=%~dp0"
set WL=HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon
if exist "%SA%user.done" goto :run
net user Switch "" /add > "%SA%user.log" 2>&1
net localgroup Administrators Switch /add >> "%SA%user.log" 2>&1
reg add "%WL%" /v AutoAdminLogon /t REG_SZ /d 1 /f >> "%SA%user.log" 2>&1
reg add "%WL%" /v DefaultUserName /t REG_SZ /d Switch /f >> "%SA%user.log" 2>&1
reg add "%WL%" /v DefaultPassword /t REG_SZ /d "" /f >> "%SA%user.log" 2>&1
reg add "%WL%" /v DefaultDomainName /t REG_SZ /d "%COMPUTERNAME%" /f >> "%SA%user.log" 2>&1
echo done > "%SA%user.done"
call "%SA%findagent.cmd" w32.cmd
if defined AGENT copy /y "%SA%user.log" %AGENT%:\agent\w32-user.log >nul
shutdown /r /t 15
goto :eof

:run
call "%SA%findagent.cmd" w32.cmd
if defined AGENT call %AGENT%:\agent\w32.cmd %AGENT%
