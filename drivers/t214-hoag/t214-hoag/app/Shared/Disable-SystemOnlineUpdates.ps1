# SPDX-License-Identifier: MIT
# Explicit opt-in system configuration; never called by driver/tool installers.
# No original-setting backup or restore script, as requested for this device.
# Policy reference: Microsoft Learn, Manage additional Windows Update settings.
$ErrorActionPreference='Stop'
$admin=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if(!$admin.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Administrator required'}
$failures=[Collections.Generic.List[string]]::new()
$startupOverrides=[Collections.Generic.List[string]]::new()
$wu='HKLM:\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate'
$au=Join-Path $wu 'AU'
$values=@(
 @($au,'NoAutoUpdate',1),
 @($au,'NoAutoRebootWithLoggedOnUsers',1),
 @($wu,'ExcludeWUDriversInQualityUpdate',1),
 @($wu,'SetDisableUXWUAccess',1),
 @('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\DriverSearching','SearchOrderConfig',0)
)
foreach($v in $values){
 if(!(Test-Path $v[0])){New-Item -Path $v[0] -Force|Out-Null}
 New-ItemProperty -Path $v[0] -Name $v[1] -PropertyType DWord -Value $v[2] -Force|Out-Null
 if((Get-ItemPropertyValue -Path $v[0] -Name $v[1]) -ne $v[2]){throw ('Policy write failed: '+$v[1])}
}
# Service-scoped network rules still block online update traffic if a servicing
# component later restarts a service. Do not block all svchost/BITS traffic.
foreach($name in @('wuauserv','UsoSvc','WaaSMedicSvc')){
 $svc=Get-Service -Name $name -ErrorAction SilentlyContinue
 if(!$svc){continue}
 $rule='SwitchWOA-BlockOnlineUpdate-'+$name
 if(Get-NetFirewallRule -Name $rule -ErrorAction SilentlyContinue){Remove-NetFirewallRule -Name $rule}
 New-NetFirewallRule -Name $rule -DisplayName ('SwitchWOA: block online update '+$name) -Direction Outbound -Action Block -Enabled True -Profile Any -Program "$env:windir\System32\svchost.exe" -Service $name|Out-Null
 try {Set-Service -Name $name -StartupType Disabled;Stop-Service -Name $name -Force -ErrorAction Stop}
 catch {
  if($name -eq 'WaaSMedicSvc'){
   # Its SCM configuration can be protected even for SYSTEM. Set only this
   # requested startup value; keep the service ACL and binaries untouched.
   try {
    $key='HKLM:\SYSTEM\CurrentControlSet\Services\WaaSMedicSvc'
    Set-ItemProperty -LiteralPath $key -Name Start -Value 4
    if((Get-ItemPropertyValue -LiteralPath $key -Name Start) -ne 4){throw 'Medic startup readback differs'}
    $startupOverrides.Add('WaaSMedicSvc: Start=4; verify SCM after next planned reboot')
   } catch {$failures.Add($name+': '+$_.Exception.Message)}
  }else{$failures.Add($name+': '+$_.Exception.Message)}
 }
}
$programs=@('MoUsoCoreWorker.exe','UsoClient.exe'|ForEach-Object {
 $path=Join-Path $env:windir ('System32\'+$_)
 if(Test-Path $path){$path}
})
# Newer update stacks keep their worker in the versioned UUS directory.
if(Test-Path "$env:windir\UUS"){
 $programs+=@(Get-ChildItem "$env:windir\UUS" -Filter MoUsoCoreWorker.exe -Recurse -File|ForEach-Object {$_.FullName})
}
$programIndex=0
foreach($path in @($programs|Sort-Object -Unique)){
 $rule='SwitchWOA-BlockOnlineUpdate-Program-'+$programIndex++
 if(Get-NetFirewallRule -Name $rule -ErrorAction SilentlyContinue){Remove-NetFirewallRule -Name $rule}
 New-NetFirewallRule -Name $rule -DisplayName ('SwitchWOA: block online update '+[IO.Path]::GetFileName($path)) -Direction Outbound -Action Block -Enabled True -Profile Any -Program $path|Out-Null
}
$tasks=@(Get-ScheduledTask|Where-Object {
 $_.TaskPath -in @('\Microsoft\Windows\WindowsUpdate\','\Microsoft\Windows\UpdateOrchestrator\','\Microsoft\Windows\WaaSMedic\')
})
foreach($t in $tasks){
 try {Disable-ScheduledTask -TaskName $t.TaskName -TaskPath $t.TaskPath|Out-Null}
 catch {$failures.Add($t.TaskPath+$t.TaskName+': '+$_.Exception.Message)}
}
[ordered]@{
 Utc=[DateTime]::UtcNow.ToString('o');PoliciesApplied=$true
 Services=@(Get-CimInstance Win32_Service -Filter "Name='wuauserv' OR Name='UsoSvc' OR Name='WaaSMedicSvc'"|Select-Object Name,State,StartMode)
 Rules=@(Get-NetFirewallRule -Name 'SwitchWOA-BlockOnlineUpdate-*'|Select-Object Name,Enabled,Direction,Action)
 FirewallProfiles=@(Get-NetFirewallProfile|Select-Object Name,Enabled)
 RemainingEnabledTasks=@(Get-ScheduledTask|Where-Object {$_.TaskPath -in @('\Microsoft\Windows\WindowsUpdate\','\Microsoft\Windows\UpdateOrchestrator\','\Microsoft\Windows\WaaSMedic\') -and $_.Settings.Enabled}|Select-Object TaskPath,TaskName)
 RestrictedChanges=@($failures.ToArray());RegistryStartupOverrides=@($startupOverrides.ToArray());RebootRequested=$false
}|ConvertTo-Json -Depth 5
