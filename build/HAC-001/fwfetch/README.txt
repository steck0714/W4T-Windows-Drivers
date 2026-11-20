fwfetch reads firmware that must not be redistributed from YOUR OWN SD card (read-only, no network).
On the Switch, in an administrator PowerShell, in this folder:
  .\Install-FwFetchStartup.ps1 -Profile erista   -GpuDir auto -DriverDir auto   (auto = the Windows that is running, whatever its drive letter is; the drivers contain NO firmware: this loads it afterwards from your SD card)
From then on fwfetch runs 1 minute after every Windows start until it has found everything. Log: %ProgramData%\W4T\logs\fwfetch-startup.log
Remove: .\Install-FwFetchStartup.ps1 -Remove
