# SPDX-License-Identifier: MIT
# Run as the desktop user; uses their interactive token, never session 0.
param(
    [Parameter(Mandatory=$true)][ValidateSet('SwitchPowerTray','Pacecar')][string]$App,
    [Parameter(Mandatory=$true)][string]$Executable
)
$ErrorActionPreference = 'Stop'
$exe = (Resolve-Path -LiteralPath $Executable).Path
$sid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
$taskName = "SwitchWOA-$App-$sid"
$arguments = if ($App -eq 'SwitchPowerTray') { '--startup-profile --minimized' } else { '' }
$delay = if ($App -eq 'SwitchPowerTray') { 'PT10S' } else { 'PT60S' }
$service = New-Object -ComObject Schedule.Service
$service.Connect()
$root = $service.GetFolder('\')
$definition = $service.NewTask(0)
$definition.RegistrationInfo.Description = "$App at desktop logon"
$definition.Principal.UserId = $sid
$definition.Principal.LogonType = 3 # TASK_LOGON_INTERACTIVE_TOKEN
$definition.Principal.RunLevel = 0
$definition.Settings.Enabled = $true
$definition.Settings.DisallowStartIfOnBatteries = $false
$definition.Settings.StopIfGoingOnBatteries = $false
$definition.Settings.ExecutionTimeLimit = 'PT0S'
$definition.Settings.MultipleInstances = 2 # IgnoreNew
$trigger = $definition.Triggers.Create(9)
$trigger.UserId = $sid
$trigger.Delay = $delay
$action = $definition.Actions.Create(0)
$action.Path = $exe
$action.Arguments = $arguments
$action.WorkingDirectory = Split-Path -Parent $exe
$task = $root.RegisterTaskDefinition($taskName, $definition, 6, $sid, $null, 3, $null)
$map = 'HKCU:\Software\SwitchWOA\StartupTasks'
if (!(Test-Path -LiteralPath $map)) { New-Item -Path $map -Force | Out-Null }
New-ItemProperty -Path $map -Name $App -PropertyType String -Value $task.Path -Force | Out-Null
$run = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
if (Get-ItemProperty -LiteralPath $run -Name $App -ErrorAction SilentlyContinue) {
    Remove-ItemProperty -LiteralPath $run -Name $App
}
[pscustomobject]@{App=$App;Task=$task.Path;Enabled=$task.Enabled;Executable=$exe;Delay=$delay}
