@echo off
rem Started by the swagent service on every boot (Windows 10 ARM64): runs \agent\w64.cmd from the
rem boot partition, with its drive letter as %1. The letter is found at run time (findagent.cmd).
call "%~dp0findagent.cmd" w64.cmd
if defined AGENT call %AGENT%:\agent\w64.cmd %AGENT%
