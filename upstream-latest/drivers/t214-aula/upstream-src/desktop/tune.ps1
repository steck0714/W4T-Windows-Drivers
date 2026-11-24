# Lightweight Windows 11 for the Switch: background services, tasks and effects that cost the SD card,
# the 4 GB of RAM or the GPU and give nothing on this console. Called by w64.cmd on every boot while
# agent\tune.flag exists (each step checks first, so a repeated run changes nothing); with
# agent\untune.flag instead, everything is put back from the backup and the backup is removed.
#   powershell -NoProfile -ExecutionPolicy Bypass -File tune.ps1 AGENT_DIR [undo]
# The original value of every setting is saved once in C:\swagent\tune-backup.json before it is
# first changed. Kept on purpose: Wi-Fi, Bluetooth, audio, touch keyboard (TabletInputService),
# Windows Update, Delivery Optimization and the update orchestrator (language packs and Windows
# features are installed through them; automatic updates stay off by the agent's policies),
# Windows Firewall, Defender's engine (only its scans and cloud reporting are turned off), SSH,
# the event log, the Start menu and its search box (only the file indexer is off).
param([string]$Agent, [string]$Mode = 'apply')
$ErrorActionPreference = 'Continue'
$log = Join-Path $Agent 'tune-log.txt'
$bk = 'C:\swagent\tune-backup.json'
function Log([string]$t) { Add-Content -Path $log -Value $t -Encoding ascii }
if ((Test-Path $log) -and (Get-Item $log).Length -gt 200KB) { Move-Item $log "$log.old" -Force }
Log "==== tune $Mode $(Get-Date -Format s)"

# ---- what is changed ----
$services = @(
    'WSearch',           # file indexer: constant SD reads and writes; Start search still finds apps
    'SysMain',           # Superfetch: preloads apps into the scarce RAM, reads the SD in the background
    'DiagTrack',         # telemetry
    'dmwappushservice',  # telemetry push routing
    'MapsBroker',        # offline maps downloads
    'lfsvc',             # geolocation (no GPS)
    'RetailDemo',
    'WerSvc',            # error report uploads (the event log still records crashes)
    'PcaSvc',            # program compatibility assistant
    'Spooler',           # printing
    'Fax',
    'WbioSrvc',          # biometrics (no sensor)
    'wisvc',             # Insider
    'XblAuthManager', 'XblGameSave', 'XboxNetApiSvc', 'XboxGipSvc',
    'WpcMonSvc',         # parental controls
    'PhoneSvc',          # telephony
    'TrkWks'             # distributed link tracking
)
$tasks = @(
    '\Microsoft\Windows\Application Experience\Microsoft Compatibility Appraiser',
    '\Microsoft\Windows\Application Experience\ProgramDataUpdater',
    '\Microsoft\Windows\Application Experience\StartupAppTask',
    '\Microsoft\Windows\Application Experience\PcaPatchDbTask',
    '\Microsoft\Windows\Customer Experience Improvement Program\Consolidator',
    '\Microsoft\Windows\Customer Experience Improvement Program\UsbCeip',
    '\Microsoft\Windows\Defrag\ScheduledDefrag',               # "optimize drives" defragments the SD card
    '\Microsoft\Windows\DiskDiagnostic\Microsoft-Windows-DiskDiagnosticDataCollector',
    '\Microsoft\Windows\Feedback\Siuf\DmClient',
    '\Microsoft\Windows\Feedback\Siuf\DmClientOnScenarioDownload',
    '\Microsoft\Windows\Maps\MapsToastTask',
    '\Microsoft\Windows\Maps\MapsUpdateTask',
    '\Microsoft\Windows\Windows Error Reporting\QueueReporting',
    '\Microsoft\Windows\Maintenance\WinSAT',
    '\Microsoft\Windows\Windows Defender\Windows Defender Scheduled Scan',
    '\Microsoft\Windows\Windows Defender\Windows Defender Cache Maintenance',
    '\Microsoft\Windows\Windows Defender\Windows Defender Cleanup',
    '\Microsoft\Windows\Windows Defender\Windows Defender Verification',
    '\Microsoft\Windows\CloudExperienceHost\CreateObjectTask',
    '\Microsoft\Windows\Flighting\FeatureConfig\UsageDataReporting',
    '\Microsoft\Windows\Flighting\OneSettings\RefreshCache'
)
# Registry values: path, name, type, value. HKCU values go to every user profile through HKU.
$machine = @(
    @('HKLM:\SOFTWARE\Policies\Microsoft\Windows\DataCollection', 'AllowTelemetry', 'DWord', 0),
    @('HKLM:\SOFTWARE\Policies\Microsoft\Dsh', 'AllowNewsAndInterests', 'DWord', 0),          # Widgets
    @('HKLM:\SOFTWARE\Policies\Microsoft\Windows\Windows Chat', 'ChatIcon', 'DWord', 3),
    @('HKLM:\SOFTWARE\Policies\Microsoft\Windows\WindowsCopilot', 'TurnOffWindowsCopilot', 'DWord', 1),
    @('HKLM:\SOFTWARE\Policies\Microsoft\Windows\CloudContent', 'DisableWindowsConsumerFeatures', 'DWord', 1),
    @('HKLM:\SOFTWARE\Policies\Microsoft\Windows\AppPrivacy', 'LetAppsRunInBackground', 'DWord', 2),
    @('HKLM:\SOFTWARE\Policies\Microsoft\Edge', 'StartupBoostEnabled', 'DWord', 0),
    @('HKLM:\SOFTWARE\Policies\Microsoft\Edge', 'BackgroundModeEnabled', 'DWord', 0),
    @('HKLM:\SOFTWARE\Policies\Microsoft\Windows\Windows Search', 'AllowCortana', 'DWord', 0),
    @('HKLM:\SOFTWARE\Policies\Microsoft\Windows\Windows Search', 'DisableWebSearch', 'DWord', 1),
    @('HKLM:\SOFTWARE\Policies\Microsoft\Windows Defender\Scan', 'DisableCatchupFullScan', 'DWord', 1),
    @('HKLM:\SOFTWARE\Policies\Microsoft\Windows Defender\Scan', 'DisableCatchupQuickScan', 'DWord', 1),
    @('HKLM:\SOFTWARE\Policies\Microsoft\Windows Defender\Spynet', 'SpynetReporting', 'DWord', 0),
    @('HKLM:\SOFTWARE\Policies\Microsoft\Windows Defender\Spynet', 'SubmitSamplesConsent', 'DWord', 2)
)
# Not changed: transparency and window/taskbar animations. With them off, the GPU desktop hung on two
# boots out of two right after the switch (DWM composes differently); with them on it did not.
$user = @(
    @('Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced', 'TaskbarDa', 'DWord', 0),       # Widgets button
    @('Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced', 'TaskbarMn', 'DWord', 0),       # Chat button
    @('Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced', 'ShowCopilotButton', 'DWord', 0),
    @('Software\Microsoft\Windows\CurrentVersion\BackgroundAccessApplications', 'GlobalUserDisabled', 'DWord', 1),
    @('Software\Microsoft\Windows\CurrentVersion\Search', 'BackgroundAppGlobalToggle', 'DWord', 0),
    @('Software\Microsoft\Windows\CurrentVersion\ContentDeliveryManager', 'SilentInstalledAppsEnabled', 'DWord', 0),
    @('Software\Microsoft\Windows\CurrentVersion\ContentDeliveryManager', 'SystemPaneSuggestionsEnabled', 'DWord', 0),
    @('Software\Microsoft\Windows\CurrentVersion\ContentDeliveryManager', 'SubscribedContent-338388Enabled', 'DWord', 0),
    @('Software\Microsoft\Windows\CurrentVersion\ContentDeliveryManager', 'SubscribedContent-338389Enabled', 'DWord', 0),
    @('Software\Microsoft\Windows\CurrentVersion\ContentDeliveryManager', 'SubscribedContent-353694Enabled', 'DWord', 0),
    @('Software\Microsoft\Windows\CurrentVersion\ContentDeliveryManager', 'SoftLandingEnabled', 'DWord', 0),
    @('Software\Microsoft\Windows\CurrentVersion\UserProfileEngagement', 'ScoobeSystemSettingEnabled', 'DWord', 0),
    @('Software\Microsoft\Windows\CurrentVersion\AdvertisingInfo', 'Enabled', 'DWord', 0)
)
$runEntries = @('OneDriveSetup', 'OneDrive', 'MicrosoftEdgeAutoLaunch_*')   # HKCU Run, per profile

# ---- backup ----
$backup = $null
if (Test-Path $bk) { $backup = Get-Content $bk -Raw | ConvertFrom-Json }
function NewBackup {
    [pscustomobject]@{ services = [pscustomobject]@{}; tasks = [pscustomobject]@{}; values = @(); run = @();
        fs = [pscustomobject]@{} }
}
# Profiles of real users (S-1-5-21-...) whose hive is loaded, i.e. the user is logged on. A hive is
# never loaded by this script: loading one while the autologon starts gives the user a temporary
# profile. User values are therefore applied (and undone) only while that user is logged on.
$script:loaded = @()
function UserHives {
    Get-ChildItem 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProfileList' |
        Where-Object { $_.PSChildName -match '^S-1-5-21-[\d-]+$' } | ForEach-Object {
            $sid = $_.PSChildName
            if (Test-Path "Registry::HKEY_USERS\$sid") { [pscustomobject]@{ sid = $sid; path = "Registry::HKEY_USERS\$sid" } }
        }
}
# Backup entries name user values by SID ("Registry::HKEY_USERS\<SID>\..."); Actual() maps such a
# path to where that hive is loaded now.
$script:hiveMap = @{}
function Actual([string]$path) {
    foreach ($sid in $script:hiveMap.Keys) {
        $logical = "Registry::HKEY_USERS\$sid"
        if ($path.StartsWith($logical)) { return $script:hiveMap[$sid] + $path.Substring($logical.Length) }
    }
    return $path
}
function GetValue($path, $name) {
    $p = Get-ItemProperty -Path $path -Name $name -ErrorAction SilentlyContinue
    if ($null -eq $p) { return $null }
    return $p.$name
}

foreach ($h in @(UserHives)) { $script:hiveMap[$h.sid] = $h.path }
function Finish { [gc]::Collect(); foreach ($name in $script:loaded) { reg unload "HKU\$name" 2>&1 | Out-Null } }

if ($Mode -eq 'undo') {
    if (-not $backup) { Log 'no backup, nothing to undo'; Finish; exit 0 }
    foreach ($s in $backup.services.PSObject.Properties) {
        $start = $s.Value
        Set-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Services\$($s.Name)" -Name Start -Value $start -ErrorAction SilentlyContinue
        Log "service $($s.Name) start=$start"
    }
    foreach ($t in $backup.tasks.PSObject.Properties) {
        if ($t.Value -ne 'Disabled') {
            $path = $t.Name.Substring(0, $t.Name.LastIndexOf('\') + 1); $name = $t.Name.Substring($t.Name.LastIndexOf('\') + 1)
            Enable-ScheduledTask -TaskPath $path -TaskName $name -ErrorAction SilentlyContinue | Out-Null
            Log "task enabled $($t.Name)"
        }
    }
    $pending = @()
    foreach ($v in $backup.values) {
        if ($v.path -like 'Registry::HKEY_USERS\*' -and -not $script:hiveMap.ContainsKey(($v.path -split '\\')[1])) { $pending += $v; continue }
        if ($null -eq $v.old) { Remove-ItemProperty -Path (Actual $v.path) -Name $v.name -ErrorAction SilentlyContinue }
        else { Set-ItemProperty -Path (Actual $v.path) -Name $v.name -Value $v.old -ErrorAction SilentlyContinue }
        Log "value $($v.path)\$($v.name) <- $($v.old)"
    }
    foreach ($r in $backup.run) {
        New-ItemProperty -Path (Actual $r.path) -Name $r.name -Value $r.value -PropertyType String -Force -ErrorAction SilentlyContinue | Out-Null
        Log "run entry restored $($r.name)"
    }
    if ($backup.fs.disablelastaccess) { fsutil behavior set disablelastaccess $backup.fs.disablelastaccess | Out-Null }
    if ($pending.Count -gt 0) {
        # the user is not logged on yet: keep only these values for the next run
        [pscustomobject]@{ services = [pscustomobject]@{}; tasks = [pscustomobject]@{}; values = $pending; run = @($backup.run);
            fs = [pscustomobject]@{} } | ConvertTo-Json -Depth 5 | Set-Content $bk -Encoding UTF8
        Log "user values pending: $($pending.Count) (restored once the user is logged on)"
    } else { Remove-Item $bk -Force }
    Finish
    Log 'undo done (takes full effect after a reboot)'
    exit 0
}

if (-not $backup) { $backup = NewBackup; $fresh = $true } else { $fresh = $false }
function Remember($kind, $key, $value) {
    if ($kind -eq 'services' -or $kind -eq 'tasks' -or $kind -eq 'fs') {
        if (-not ($backup.$kind.PSObject.Properties.Name -contains $key)) {
            $backup.$kind | Add-Member -NotePropertyName $key -NotePropertyValue $value
        }
    }
}
$changed = 0

# Services: Start value 4 = disabled. Protected services refuse sc config, but SYSTEM
# can write the registry value.
foreach ($s in $services) {
    $key = "HKLM:\SYSTEM\CurrentControlSet\Services\$s"
    if (-not (Test-Path $key)) { continue }
    $start = (Get-ItemProperty $key -Name Start -ErrorAction SilentlyContinue).Start
    if ($null -eq $start -or $start -eq 4) { continue }
    Remember 'services' $s $start
    Stop-Service $s -Force -ErrorAction SilentlyContinue
    Set-ItemProperty $key -Name Start -Value 4 -ErrorAction SilentlyContinue
    Log "service $s start $start -> 4"; $changed++
}

# Services an older version of this script turned off but this one keeps (Windows Update, Delivery
# Optimization and the update orchestrator before 2026-11-15): back to their saved start value.
if (-not $fresh) {
    foreach ($p in @($backup.services.PSObject.Properties)) {
        if ($services -contains $p.Name) { continue }
        Set-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Services\$($p.Name)" -Name Start -Value $p.Value -ErrorAction SilentlyContinue
        $backup.services.PSObject.Properties.Remove($p.Name)
        Log "service $($p.Name) no longer managed, start back to $($p.Value)"; $changed++
    }
}

foreach ($t in $tasks) {
    $path = $t.Substring(0, $t.LastIndexOf('\') + 1); $name = $t.Substring($t.LastIndexOf('\') + 1)
    $task = Get-ScheduledTask -TaskPath $path -TaskName $name -ErrorAction SilentlyContinue
    if (-not $task -or $task.State -eq 'Disabled') { continue }
    Remember 'tasks' $t ([string]$task.State)
    try { Disable-ScheduledTask -InputObject $task -ErrorAction Stop | Out-Null; Log "task disabled $t"; $changed++ }
    catch { Log "task kept $t ($($_.Exception.Message))" }
}

function SetValue($logical, $name, $type, $value) {
    $path = Actual $logical
    $old = GetValue $path $name
    if ($null -ne $old -and "$old" -eq "$value") { return }
    if (-not ($backup.values | Where-Object { $_.path -eq $logical -and $_.name -eq $name })) {
        $backup.values = @($backup.values) + [pscustomobject]@{ path = $logical; name = $name; old = $old }
    }
    if (-not (Test-Path $path)) { New-Item -Path $path -Force | Out-Null }
    New-ItemProperty -Path $path -Name $name -PropertyType $type -Value $value -Force | Out-Null
    Log "value $logical\$name $old -> $value"; $script:changed++
}
foreach ($v in $machine) { SetValue $v[0] $v[1] $v[2] $v[3] }
foreach ($sid in @($script:hiveMap.Keys)) {
    $h = "Registry::HKEY_USERS\$sid"
    foreach ($v in $user) { SetValue "$h\$($v[0])" $v[1] $v[2] $v[3] }
    $run = "$h\Software\Microsoft\Windows\CurrentVersion\Run"
    $props = Get-ItemProperty (Actual $run) -ErrorAction SilentlyContinue
    if ($props) {
        foreach ($p in $props.PSObject.Properties) {
            foreach ($pat in $runEntries) {
                if ($p.Name -like $pat) {
                    if (-not (@($backup.run) | Where-Object { $_.path -eq $run -and $_.name -eq $p.Name })) {
                        $backup.run = @($backup.run) + [pscustomobject]@{ path = $run; name = $p.Name; value = [string]$p.Value }
                    }
                    Remove-ItemProperty -Path (Actual $run) -Name $p.Name -ErrorAction SilentlyContinue
                    Log "run entry removed $($p.Name)"; $changed++
                }
            }
        }
    }
}

Finish

# NTFS: no last-access time updates (one extra write per file read on the SD card).
$la = (fsutil behavior query disablelastaccess) -join ' '
if ($la -notmatch '= 1\b') {
    $cur = if ($la -match '= (\d)') { $Matches[1] } else { '2' }
    Remember 'fs' 'disablelastaccess' $cur
    fsutil behavior set disablelastaccess 1 | Out-Null
    Log "fsutil disablelastaccess $cur -> 1"; $changed++
}

if ($changed -gt 0 -or $fresh) { $backup | ConvertTo-Json -Depth 5 | Set-Content $bk -Encoding UTF8 }
Log "changed $changed"
