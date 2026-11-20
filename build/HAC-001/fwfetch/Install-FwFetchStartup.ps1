<#
.SYNOPSIS
  Registers a scheduled task that runs FwFetchStartup.ps1 at every Windows start (as SYSTEM), so the firmware that cannot
  be shipped with the drivers is loaded from your own SD card automatically. Run once, in an administrator PowerShell.

  The files stay where they are: this script copies fwfetch.exe + FwFetchStartup.ps1 to C:\ProgramData\W4T\fwfetch
  (the task must not depend on a folder that may disappear), and the task runs them from there.

.EXAMPLE
  .\Install-FwFetchStartup.ps1 -Profile lite -GpuDir C:\Windows\System32\drivers\TegraGpu
  .\Install-FwFetchStartup.ps1 -Profile lite -GpuDir C:\Windows\System32\drivers\TegraGpu -DriverDir C:\Windows\System32\drivers
  .\Install-FwFetchStartup.ps1 -Profile aula
  .\Install-FwFetchStartup.ps1 -Remove
#>
param(
  [ValidateSet('erista', 'lite', 'aula')][string]$Profile,
  [string]$GpuDir = '',
  [string]$DriverDir = '',   # e.g. C:\Windows\System32\drivers (Wi-Fi / Bluetooth firmware for the SwitchWOA drivers)
  [string]$Exe = (Join-Path $PSScriptRoot 'fwfetch.exe'),
  [switch]$Remove
)
$ErrorActionPreference = 'Stop'
$taskName = 'W4T fwfetch at startup'
$dest = Join-Path $env:ProgramData 'W4T\fwfetch'

if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
  throw 'Run this in an administrator PowerShell.'
}
if ($Remove) {
  Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
  Write-Host "removed task '$taskName' (files in $dest and the cache are left in place)"
  return
}
if (-not $Profile) { throw '-Profile erista|lite|aula is required' }
if (-not (Test-Path $Exe)) { throw "fwfetch.exe not found: $Exe (build it with .github/workflows/build-fwfetch.yml, use the aarch64 one on the Switch)" }

New-Item -ItemType Directory -Force $dest | Out-Null
Copy-Item $Exe (Join-Path $dest 'fwfetch.exe') -Force
Copy-Item (Join-Path $PSScriptRoot 'FwFetchStartup.ps1') $dest -Force

$arg = "-NoProfile -ExecutionPolicy Bypass -File `"$dest\FwFetchStartup.ps1`" -Profile $Profile"
if ($GpuDir) { $arg += " -GpuDir `"$GpuDir`"" }
if ($DriverDir) { $arg += " -DriverDir `"$DriverDir`"" }
$action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument $arg
$trigger = New-ScheduledTaskTrigger -AtStartup
$trigger.Delay = 'PT1M'    # give the SD / storage stack time to come up
$daily = New-ScheduledTaskTrigger -Daily -At 3:30am   # also re-checks (and repairs) files damaged while Windows stays up
$principal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet -StartWhenAvailable -ExecutionTimeLimit (New-TimeSpan -Minutes 30) -MultipleInstances IgnoreNew
Register-ScheduledTask -TaskName $taskName -Action $action -Trigger @($trigger, $daily) -Principal $principal -Settings $settings -Force | Out-Null
Write-Host "registered '$taskName' (profile $Profile). It runs 1 minute after every start (and daily at 03:30): it checks every firmware file against its pinned SHA-256, puts damaged or missing ones back (from the cache, else from the SD) and keeps trying until everything is found; log: $env:ProgramData\W4T\logs\fwfetch-startup.log"
Write-Host "run it now: Start-ScheduledTask -TaskName '$taskName'"
