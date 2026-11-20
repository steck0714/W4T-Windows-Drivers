fwfetch reads firmware that must not be redistributed from YOUR OWN SD card (read-only, no network).
On the Switch, in an administrator PowerShell, in this folder:
  .\Install-FwFetchStartup.ps1 -Profile erista   -GpuDir C:\Windows\System32\drivers\TegraGpu -DriverDir C:\Windows\System32\drivers   (the drivers contain NO firmware: this loads it afterwards from your SD card)
From then on fwfetch runs 1 minute after every Windows start until it has found everything. Log: C:\ProgramData\W4T\logs\fwfetch-startup.log
Remove: .\Install-FwFetchStartup.ps1 -Remove
