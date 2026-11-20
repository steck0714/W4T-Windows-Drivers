@echo off
rem Sets AGENT to the drive letter of the small FAT boot partition (the one holding \agent\w64.cmd).
rem   call findagent.cmd [file]      file defaults to w64.cmd; a script that looks for another file passes its name
rem The letter is not fixed: on an SD card the boot partition can be D:, E: or any other letter, and with a
rem multi-partition card (Switchroot and similar layouts) Windows often gives it no letter at all.
rem   1. every letter A-Z is tried first (nothing is changed)
rem   2. then every volume without a letter is looked at through its \\?\Volume{GUID}\ path; a match gets a free letter
rem   3. if cmd cannot open that path, each volume gets a free letter for a moment and the one that matches keeps it
rem      (mountvol assignments persist, so the next boot finds it in step 1)
rem Volume names seen on Switch SD cards (not used for the decision, only \agent\<file> is: names can be changed):
rem   CRASHDUMP  = windows-switch boot partition; "SWITCH SD" = what hekate's partition manager and TegraExplorer
rem   give the FAT32 partition (hekate: FF_MKFS_LABEL "SWITCH SD "; the boot partition may also be exFAT since hekate 6.1.0).
rem The letter used is always a free one, picked at run time (never a fixed W: / S:): the first mountvol that works wins.
setlocal
set "MARK=%~1"
if not defined MARK set "MARK=w64.cmd"
set "FOUND="
for %%d in (A B C D E F G H I J K L M N O P Q R S T U V W X Y Z) do if not defined FOUND if exist %%d:\agent\%MARK% set "FOUND=%%d"
if defined FOUND goto :done
for /f %%v in ('mountvol 2^>nul ^| findstr /l /c:"Volume{"') do if not defined FOUND call :viavolume "%%v"
if defined FOUND goto :done
for /f %%v in ('mountvol 2^>nul ^| findstr /l /c:"Volume{"') do if not defined FOUND call :trymount "%%v"
:done
endlocal & set "AGENT=%FOUND%"
goto :eof

:assign
rem ASSIGNED = the letter (Z down to D) that mountvol could give to volume %~1, empty when none worked
set "ASSIGNED="
for %%L in (Z Y X W V U T S R Q P O N M L K J I H G F E D) do if not defined ASSIGNED if not exist %%L:\ mountvol %%L: "%~1" >nul 2>&1 && set "ASSIGNED=%%L"
goto :eof

:viavolume
if not exist "%~1agent\%MARK%" goto :eof
call :assign "%~1"
if defined ASSIGNED if exist %ASSIGNED%:\agent\%MARK% set "FOUND=%ASSIGNED%"
goto :eof

:trymount
call :assign "%~1"
if not defined ASSIGNED goto :eof
if exist %ASSIGNED%:\agent\%MARK% (set "FOUND=%ASSIGNED%") else mountvol %ASSIGNED%: /d >nul 2>&1
goto :eof
