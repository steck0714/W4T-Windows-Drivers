<#
.SYNOPSIS
  Registers a scheduled task that runs FwFetchStartup.ps1 at every Windows start (as SYSTEM), so the firmware that cannot
  be shipped with the drivers is loaded from your own SD card automatically. Run once, in an administrator PowerShell.

  The files stay where they are: this script copies fwfetch.exe + FwFetchStartup.ps1 to <ProgramData>\W4T\fwfetch
  (the task must not depend on a folder that may disappear), and the task runs them from there.

  Windows on a Switch is on the SD card and is often NOT C: - nothing here assumes a letter:
    * -GpuDir auto / -DriverDir auto  = this Windows' <SystemRoot>\System32\drivers\TegraGpu / <SystemRoot>\System32\drivers (recommended).
    * A path you type that is inside this Windows' System32\drivers is stored as 'auto', so the task keeps working if the letter changes.
    * The task finds its own script through %ProgramData% when it runs (no letter is written into the task).

.EXAMPLE
  .\Install-FwFetchStartup.ps1 -Profile lite -GpuDir auto
  .\Install-FwFetchStartup.ps1 -Profile lite -GpuDir auto -DriverDir auto
  .\Install-FwFetchStartup.ps1 -Profile lite -GpuDir auto -DriverDir auto -SearchRoot $env:ProgramData\W4T\fw-source   # no Switchroot on the Switch: the downloaded files sit in that folder
  .\Install-FwFetchStartup.ps1 -Profile aula
  .\Install-FwFetchStartup.ps1 -Remove
#>
param(
  [ValidateSet('erista', 'lite', 'aula')][string]$Profile,
  [string]$GpuDir = '',
  [string[]]$SearchRoot = @(),   # folder(s) with the DOWNLOADED Switchroot files (bl31.bin, switchroot\install\l4t.00 / l4t.01 ...) on this Windows disk. Switchroot is NOT installed
  [string[]]$Disk = @(),         # image file(s) / raw disk(s) with an ext4 root file system, read-only
  [string]$DriverDir = '',   # 'auto' = this Windows' System32\drivers (Wi-Fi / Bluetooth firmware for the SwitchWOA drivers)
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

# Paths inside THIS Windows' System32\drivers are stored as 'auto': a letter written into the task would break as soon as the SD card's
# Windows gets another one (it is rarely C:). Anything else is kept as typed (single-quoted for PowerShell).
$sysDrivers = Join-Path $env:SystemRoot 'System32\drivers'
function ConvertTo-TaskDir([string]$p, [string]$sub) {
  if (-not $p) { return '' }
  if ($p -eq 'auto') { return 'auto' }
  $full = [IO.Path]::GetFullPath([Environment]::ExpandEnvironmentVariables($p)).TrimEnd('\')
  $want = $sysDrivers; if ($sub) { $want = Join-Path $sysDrivers $sub }
  if ($full -ieq $want) { return 'auto' }
  if ($full -imatch '^[A-Za-z]:\\.*?\\System32\\drivers(\\.*)?$' -and $full -notlike "$env:SystemRoot*") {
    Write-Warning "'$p' is not on the Windows that is running now ($env:SystemRoot); the task will use this Windows instead ('auto')."
    return 'auto'
  }
  return "'" + $full.Replace("'", "''") + "'"
}
# Source folders: a path under %ProgramData% is stored relative to it (the letter of the SD's Windows may change); anything else as typed.
function ConvertTo-TaskSrc([string]$p) {
  $full = [IO.Path]::GetFullPath([Environment]::ExpandEnvironmentVariables($p)).TrimEnd('\')
  $pd = $env:ProgramData.TrimEnd('\')
  if ($full -like "$pd\*") { return "(Join-Path `$env:ProgramData '" + $full.Substring($pd.Length + 1).Replace("'", "''") + "')" }
  return "'" + $full.Replace("'", "''") + "'"
}
$gpuArg = ConvertTo-TaskDir $GpuDir 'TegraGpu'
$drvArg = ConvertTo-TaskDir $DriverDir ''

# The task runs a PowerShell command that locates the script through $env:ProgramData at run time (no drive letter is stored).
$cmd = '& (Join-Path $env:ProgramData ''W4T\fwfetch\FwFetchStartup.ps1'') -Profile ' + $Profile   # single quotes: $env:ProgramData must NOT be expanded now
if ($gpuArg) { $cmd += " -GpuDir $gpuArg" }
if ($drvArg) { $cmd += " -DriverDir $drvArg" }
if ($SearchRoot) { $cmd += ' -SearchRoot @(' + (($SearchRoot | ForEach-Object { ConvertTo-TaskSrc $_ }) -join ',') + ')' }
if ($Disk) { $cmd += ' -Disk @(' + (($Disk | ForEach-Object { ConvertTo-TaskSrc $_ }) -join ',') + ')' }
$cmd += '; exit $LASTEXITCODE'
$arg = '-NoProfile -ExecutionPolicy Bypass -Command "' + $cmd.Replace('"', '\"') + '"'
$action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument $arg
$trigger = New-ScheduledTaskTrigger -AtStartup
$trigger.Delay = 'PT1M'    # give the SD / storage stack time to come up
$daily = New-ScheduledTaskTrigger -Daily -At 3:30am   # also re-checks (and repairs) files damaged while Windows stays up
$principal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet -StartWhenAvailable -ExecutionTimeLimit (New-TimeSpan -Minutes 30) -MultipleInstances IgnoreNew
Register-ScheduledTask -TaskName $taskName -Action $action -Trigger @($trigger, $daily) -Principal $principal -Settings $settings -Force | Out-Null
Write-Host "registered '$taskName' (profile $Profile). It runs 1 minute after every start (and daily at 03:30): it checks every firmware file against its pinned SHA-256, puts damaged or missing ones back (from the cache, else from the SD) and keeps trying until everything is found; log: $env:ProgramData\W4T\logs\fwfetch-startup.log"
Write-Host "run it now: Start-ScheduledTask -TaskName '$taskName'"
