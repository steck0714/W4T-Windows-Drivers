# SPDX-License-Identifier: MIT
# Update the two existing tools in the desktop user's interactive session.
# Payload: files.json, both EXEs, and Install-StartupTask.ps1. No driver changes.
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$identity=[Security.Principal.WindowsIdentity]::GetCurrent()
$session=[Diagnostics.Process]::GetCurrentProcess().SessionId
if($session -eq 0 -or $identity.IsSystem){throw 'Run in the desktop user session'}
$homeDir=Join-Path $env:LOCALAPPDATA 'SwitchWOA'
New-Item -ItemType Directory -Path $homeDir -Force|Out-Null
$receipt=Join-Path $homeDir 'desktop-tools-update.json'
$result=[ordered]@{Utc=[DateTime]::UtcNow.ToString('o');Phase='Started';Exit=1;User=$identity.Name;Session=$session;RebootRequested=$false}
function Save-Receipt {
    $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($result|ConvertTo-Json -Depth 6))
    $stream=[IO.FileStream]::new($receipt,[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::Read,4096,[IO.FileOptions]::WriteThrough)
    try{$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
}
function Flush-File([string]$Path) {
    $stream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::Read)
    try{$stream.Flush($true)}finally{$stream.Dispose()}
}
$apps=@('SwitchPowerTray','Pacecar')
$binaries=@('SwitchPowerTray.exe','Pacecar.Switch.exe')
$taskNames=@($apps|ForEach-Object {'SwitchWOA-'+$_+'-'+$identity.User.Value})
$destinations=@(0,1|ForEach-Object {Join-Path $homeDir ($apps[$_]+'\'+$binaries[$_])})
$oldTasks=@{}
$changed=$false
$pacecarWasRunning=$false
$configChanged=$false
$configPath=Join-Path $env:APPDATA 'PacecarSwitch\config.json'
$schedule=New-Object -ComObject Schedule.Service
$schedule.Connect()
$taskRoot=$schedule.GetFolder('\')
function Stop-InstalledTools {
    foreach($process in @(Get-CimInstance Win32_Process -Filter "Name='SwitchPowerTray.exe' OR Name='Pacecar.Switch.exe'")){
        $index=[Array]::IndexOf($binaries,$process.Name)
        if($index -lt 0 -or $process.ExecutablePath -ne $destinations[$index] -or $process.SessionId -ne $session){
            throw 'An application is running outside this installation/session'
        }
        $live=Get-Process -Id $process.ProcessId -ErrorAction SilentlyContinue
        if($live){
            Stop-Process -Id $process.ProcessId -Force
            if(!$live.WaitForExit(5000)){throw ('Application did not exit within 5s: '+$process.Name)}
        }
    }
}
Save-Receipt
try {
    $manifest=Get-Content -LiteralPath (Join-Path $root 'files.json') -Raw|ConvertFrom-Json
    foreach($required in @($binaries)+@('Install-StartupTask.ps1')){
        if(@($manifest.Files|Where-Object Name -eq $required).Count -ne 1){throw ('Missing payload identity: '+$required)}
    }
    foreach($entry in $manifest.Files){
        if((Get-FileHash -LiteralPath (Join-Path $root $entry.Name)).Hash -ne $entry.Sha256){throw ('Payload mismatch: '+$entry.Name)}
    }
    if(@($manifest.Previous).Count -ne 2){throw 'Invalid Previous hash count in manifest'}
    for($i=0;$i -lt 2;$i++){
        if((Get-FileHash -LiteralPath $destinations[$i]).Hash -ne $manifest.Previous[$i]){throw 'Installed application changed'}
        # These are updates to the established per-user tasks. Capture their
        # enabled state and exact actions before changing either application.
        $oldTasks[$taskNames[$i]]=$taskRoot.GetTask($taskNames[$i]).Xml
    }
    $backup=Join-Path $homeDir ('Backups\desktop-tools-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))
    New-Item -ItemType Directory -Path $backup|Out-Null
    $result.Backup=$backup
    if($manifest.PSObject.Properties.Name -contains 'PacecarOpacity') {
        $opacity=[double]$manifest.PacecarOpacity
        if([double]::IsNaN($opacity) -or [double]::IsInfinity($opacity) -or $opacity -lt 0.1 -or $opacity -gt 1){throw 'Invalid requested opacity'}
        if(!(Test-Path $configPath)){throw 'Expected installed Pacecar configuration'}
        Copy-Item -LiteralPath $configPath -Destination (Join-Path $backup 'pacecar-config.json')
        Flush-File (Join-Path $backup 'pacecar-config.json')
    }
    for($i=0;$i -lt 2;$i++){
        Copy-Item -LiteralPath $destinations[$i] -Destination (Join-Path $backup $binaries[$i])
        [IO.File]::WriteAllText((Join-Path $backup ($apps[$i]+'.task.xml')),$oldTasks[$taskNames[$i]])
        Flush-File (Join-Path $backup $binaries[$i])
        Flush-File (Join-Path $backup ($apps[$i]+'.task.xml'))
    }
    Save-Receipt
    $pacecarWasRunning=@(Get-CimInstance Win32_Process -Filter "Name='Pacecar.Switch.exe'"|Where-Object {$_.ExecutablePath -eq $destinations[1] -and $_.SessionId -eq $session}).Count -gt 0
    Stop-InstalledTools
    $changed=$true
    if($manifest.PSObject.Properties.Name -contains 'PacecarOpacity') {
        $config=[IO.File]::ReadAllText($configPath)|ConvertFrom-Json
        $config.general.opacity=$opacity
        $configChanged=$true
        [IO.File]::WriteAllText($configPath,($config|ConvertTo-Json -Depth 16),[Text.UTF8Encoding]::new($false))
        Flush-File $configPath
        $result.PacecarOpacity=$opacity
    }
    $tasks=@()
    for($i=0;$i -lt 2;$i++){
        Copy-Item -LiteralPath (Join-Path $root $binaries[$i]) -Destination $destinations[$i] -Force
        Flush-File $destinations[$i]
        $tasks+= & (Join-Path $root 'Install-StartupTask.ps1') -App $apps[$i] -Executable $destinations[$i]
    }
    # Live UI replacement preserves the current power mode. The login task
    # retains --startup-profile for the next actual desktop logon.
    $started=@(Start-Process $destinations[0] -ArgumentList '--minimized' -PassThru)
    $started+=Start-Process $destinations[1] -PassThru
    Start-Sleep -Seconds 3
    foreach($process in $started){$process.Refresh();if($process.HasExited){throw 'Updated application exited'}}
    foreach($name in $taskNames){if(!$taskRoot.GetTask($name).Enabled){throw ('Application disabled its login task: '+$name)}}
    $result.Tasks=$tasks
    $result.Processes=@($started|Select-Object Id,ProcessName,SessionId)
    $result.Phase='Installed';$result.Exit=0;$result.BootStartupValidation='Pending actual logon'
} catch {
    $result.Error=$_.Exception.Message;$result.Phase='Failed'
    if($changed){
        try {
            Stop-InstalledTools
            if($configChanged){Copy-Item -LiteralPath (Join-Path $backup 'pacecar-config.json') -Destination $configPath -Force;Flush-File $configPath}
            for($i=0;$i -lt 2;$i++){
                Copy-Item -LiteralPath (Join-Path $backup $binaries[$i]) -Destination $destinations[$i] -Force
                Flush-File $destinations[$i]
                $null=$taskRoot.RegisterTask($taskNames[$i],$oldTasks[$taskNames[$i]],6,$identity.User.Value,$null,3,$null)
            }
            Start-Process $destinations[0] -ArgumentList '--minimized'
            if($pacecarWasRunning){Start-Process $destinations[1]}
            $result.Rollback='Previous binaries and tasks restored'
        } catch {$result.RollbackError=$_.Exception.Message}
    }
} finally {$result.End=[DateTime]::UtcNow.ToString('o');Save-Receipt}
exit $result.Exit
