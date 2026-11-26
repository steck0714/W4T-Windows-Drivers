<#
.SYNOPSIS
  Runs at every Windows start (as SYSTEM, from the scheduled task made by Install-FwFetchStartup.ps1): loads the firmware
  that cannot be shipped with the drivers from YOUR OWN SD card, read-only, with fwfetch.exe sitting next to this script -
  and repairs it when a file has gone missing or been damaged.

  Every start:
    1. Integrity check (no SD needed): every staged copy in the cache is hashed and compared with the SHA-256 pinned in
       fw-lock.json; with -GpuDir, every GPU file in the driver folder is compared the same way.
    2. Damaged deployed file but intact cache copy  -> copied back from the cache (re-checked against the lock first).
    3. Damaged / missing cache copy, unreadable lock, or the profile was never completed -> fwfetch runs again and
       stages + deploys everything it finds from the SD (this also re-pins fw-lock.json).
    4. Re-checked at the end; the result is written to the log. Nothing is left half-written (temp file + rename).
  Once a complete, verified state has been recorded (done.<profile> marker) later starts only do step 1.

  fwfetch exit code 0 = everything found, 2 = something missing (tried again at the next start: the card may not be
  readable yet, or the file is not on it), 1 = error. This script exits 0 only when the final check is clean.
  Never writes to the SD card.

  Network (default; -NoDownload turns it off): files that are STILL missing after the local search are fetched ON THIS DEVICE, at
  start-up, from the official sources in the manifest (NVIDIA's Jetson Linux package for the GPU / XUSB firmware, the hekate
  release for sc7entry/sc7exit) with curl.exe - streamed, never stored whole. Needs internet at that moment; a failed attempt is
  simply repeated at a later start, but at most every -DownloadIntervalHours hours (the NVIDIA package is about 1 GB). Wi-Fi / Bluetooth
  firmware and bl31.bin have no such source, and Wi-Fi cannot fetch its own firmware: use USB-LAN / USB tethering the first time.

  Drive letters: nothing here assumes Windows is on C:. -GpuDir / -DriverDir take 'auto' (= this Windows' System32\drivers), a path
  with %SystemRoot% / {SystemRoot}, or a plain path; a path saved by an older install whose '<letter>:\...\System32\drivers' letter is no
  longer the running Windows is moved to the running Windows (and logged). The SD card's FAT32 'switchroot' partition is found even when
  it has no drive letter (fwfetch --auto also searches volumes by their \\?\Volume{GUID}\ name).
#>
param(
  [Parameter(Mandatory = $true)][ValidateSet('erista', 'lite', 'aula')][string]$Profile,
  [string]$CacheDir = (Join-Path $env:ProgramData 'W4T\fw-cache'),
  [string]$GpuDir = '',            # 'auto' = <SystemRoot>\System32\drivers\TegraGpu of THIS Windows (erista / lite). Windows on a Switch is rarely on C: (it lives on the SD card), so do not type a drive letter
  [string]$DriverDir = '',         # 'auto' = <SystemRoot>\System32\drivers of THIS Windows: Wi-Fi / Bluetooth firmware is put into <DriverDir>\SwitchWOA and \switchwifi (needs fwfetch --deploy-drivers)
  [switch]$Force,                  # ignore the done marker and always run fwfetch
  [switch]$NoDownload,             # never download (SD card / local files only)
  [int]$DownloadIntervalHours = 6  # a failed download is not repeated sooner than this
)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $here 'fwfetch.exe'
$logDir = Join-Path $env:ProgramData 'W4T\logs'
New-Item -ItemType Directory -Force $logDir, $CacheDir | Out-Null
$log = Join-Path $logDir 'fwfetch-startup.log'
$marker = Join-Path $CacheDir "done.$Profile"
$lockPath = Join-Path $CacheDir 'fw-lock.json'
function Log([string]$m) { Add-Content -Path $log -Value ("{0:u} {1}" -f (Get-Date), $m) -Encoding utf8 }

# ---- where the RUNNING Windows lives (never assume C:) ----------------------------------------------------------------
$sysRoot = $env:SystemRoot; if (-not $sysRoot) { $sysRoot = $env:windir }
if (-not $sysRoot) { Log 'SystemRoot is not set: cannot tell where this Windows lives'; exit 1 }
$sysDrivers = Join-Path $sysRoot 'System32\drivers'
function Resolve-W4TDir([string]$p, [string]$autoSub) {
  if (-not $p) { return '' }
  if ($p -eq 'auto') { if ($autoSub) { return (Join-Path $sysDrivers $autoSub) } else { return $sysDrivers } }
  $q = $p.Replace('{SystemRoot}', $sysRoot).Replace('{SystemDrive}', (Split-Path -Qualifier $sysRoot))
  $q = [Environment]::ExpandEnvironmentVariables($q)
  # a path saved by an older install: '<letter>:\...\System32\drivers[\x]' with a letter that is not this Windows any more -> follow this Windows
  $m = [regex]::Match($q, '^[A-Za-z]:\\.*?\\System32\\drivers(?<rest>(\\.*)?)$', 'IgnoreCase')
  if ($m.Success -and -not $q.StartsWith($sysRoot, [StringComparison]::OrdinalIgnoreCase)) {
    $moved = $sysDrivers + $m.Groups['rest'].Value
    Log "path '$p' is not on the running Windows ($sysRoot): using '$moved' instead"
    return $moved
  }
  return $q
}
function Test-DriveExists([string]$p) {
  # never let New-Item invent a folder on a drive that is not there (or is another disk's) - stop and say so
  try { $qual = Split-Path -Qualifier $p -ErrorAction Stop } catch { return $true }   # relative / UNC: nothing to check
  return (Test-Path -LiteralPath ($qual + '\'))
}
$GpuDir = Resolve-W4TDir $GpuDir 'TegraGpu'
$DriverDir = Resolve-W4TDir $DriverDir ''
foreach ($d in @($GpuDir, $DriverDir)) { if ($d -and -not (Test-DriveExists $d)) { Log "drive of '$d' does not exist (the drive letter changed?). Re-run Install-FwFetchStartup.ps1 with -GpuDir auto -DriverDir auto"; exit 1 } }
Log "Windows is on $sysRoot; GpuDir='$GpuDir' DriverDir='$DriverDir' CacheDir='$CacheDir'"

# keep the log small
if ((Test-Path $log) -and (Get-Item $log).Length -gt 512KB) { Move-Item $log "$log.old" -Force }

function Test-SafeRel([string]$p) {
  # fw-lock.json is read by SYSTEM: never join a path that could leave the cache / driver folder
  return ($p -and $p -notmatch '[\\:]' -and -not $p.StartsWith('/') -and @(($p -split '/') | Where-Object { $_ -eq '' -or $_ -eq '.' -or $_ -eq '..' }).Count -eq 0)
}
function Get-Sha([string]$f) { (Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower() }

function Read-Lock {
  if (-not (Test-Path $lockPath)) { return $null }
  try { return (Get-Content $lockPath -Raw | ConvertFrom-Json) } catch { Log "fw-lock.json is unreadable: $($_.Exception.Message)"; return $null }
}

# returns @{ Lock = ...; Cache = @(problems); Deployed = @(problems) } ; a problem is { Id; Path; Why; Want }
function Test-Integrity {
  $r = @{ Lock = $null; Cache = @(); Deployed = @(); Missing = @() }
  $lock = Read-Lock
  $r.Lock = $lock
  if ($null -eq $lock) { return $r }
  foreach ($p in $lock.PSObject.Properties) {
    $id = $p.Name; $e = $p.Value
    if (-not $e.sha256 -or -not (Test-SafeRel $e.staged)) { $r.Cache += [pscustomobject]@{ Id = $id; Path = "$($e.staged)"; Why = 'bad lock entry'; Want = '' }; continue }
    $f = Join-Path $CacheDir ($e.staged -replace '/', '\')
    if (-not (Test-Path -LiteralPath $f -PathType Leaf)) { $r.Cache += [pscustomobject]@{ Id = $id; Path = $f; Why = 'missing'; Want = $e.sha256 } }
    elseif ((Get-Sha $f) -ne $e.sha256) { $r.Cache += [pscustomobject]@{ Id = $id; Path = $f; Why = 'hash differs'; Want = $e.sha256 } }
    if ($DriverDir -and $e.drivers) {
      # existence only: the Wi-Fi board file is written with this Switch's MAC, so its bytes differ from the staged copy on purpose
      foreach ($t in @($e.drivers)) {
        if (-not (Test-SafeRel "$t")) { $r.Cache += [pscustomobject]@{ Id = $id; Path = "$t"; Why = 'bad drivers path in lock'; Want = '' }; continue }
        $d = Join-Path $DriverDir ($t -replace '/', '\')
        if (-not (Test-Path -LiteralPath $d -PathType Leaf)) { $r.Missing += [pscustomobject]@{ Id = $id; Path = $d; Why = 'missing'; Want = '' } }
      }
    }
    if ($GpuDir -and ($e.staged -like 'gpu/*')) {
      $d = Join-Path $GpuDir (Split-Path $e.staged -Leaf)
      if (-not (Test-Path -LiteralPath $d -PathType Leaf)) { $r.Deployed += [pscustomobject]@{ Id = $id; Path = $d; Why = 'missing'; Want = $e.sha256 } }
      elseif ((Get-Sha $d) -ne $e.sha256) { $r.Deployed += [pscustomobject]@{ Id = $id; Path = $d; Why = 'hash differs'; Want = $e.sha256 } }
    }
  }
  return $r
}

if (-not (Test-Path $exe)) { Log "fwfetch.exe not found next to the script ($here)"; exit 1 }

# ---- 1. integrity check (no SD needed) ---------------------------------------------------------------------------
$chk = Test-Integrity
$needFetch = [bool]$Force
if ($null -eq $chk.Lock) { $needFetch = $true; Log 'no usable fw-lock.json: the firmware has not been loaded yet (or the lock was damaged)' }
elseif (-not (Test-Path $marker)) { $needFetch = $true; Log "profile $Profile was never completed" }
foreach ($x in $chk.Cache) { Log "cache: $($x.Id) $($x.Why): $($x.Path)"; $needFetch = $true }
foreach ($x in $chk.Missing) { Log "driver file $($x.Why): $($x.Path)"; $needFetch = $true }
# GPU driver folder never filled although the profile needs it
if ($GpuDir -and $null -ne $chk.Lock -and -not (@($chk.Lock.PSObject.Properties | Where-Object { $_.Value.staged -like 'gpu/*' }).Count)) { $needFetch = $true; Log 'lock has no GPU files yet' }

# ---- 2. deployed file damaged, cache copy intact -> copy it back (the copy is re-hashed against the lock first) ----
$stillBad = @()
foreach ($x in $chk.Deployed) {
  Log "deployed: $($x.Id) $($x.Why): $($x.Path)"
  $src = Join-Path $CacheDir ($chk.Lock.($x.Id).staged -replace '/', '\')
  if ((Test-Path -LiteralPath $src -PathType Leaf) -and ((Get-Sha $src) -eq $x.Want)) {
    $tmp = "$($x.Path).tmp"
    try {
      New-Item -ItemType Directory -Force (Split-Path $x.Path) | Out-Null
      Copy-Item -LiteralPath $src -Destination $tmp -Force
      if ((Get-Sha $tmp) -ne $x.Want) { throw 'copy does not match the lock' }
      Move-Item -LiteralPath $tmp -Destination $x.Path -Force
      Log "repaired from the cache: $($x.Path)"
    } catch { Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue; Log "could not repair $($x.Path) from the cache: $($_.Exception.Message)"; $stillBad += $x }
  } else { $stillBad += $x }
}
if ($stillBad.Count) { $needFetch = $true }

# ---- 3. run fwfetch when something is missing / damaged -----------------------------------------------------------
$code = 0
if ($needFetch) {
  if (Test-Path $marker) { Remove-Item $marker -Force }
  $a = @('--profile', $Profile, '--auto', '--out-dir', $CacheDir)
  $stamp = Join-Path $CacheDir 'download.stamp'
  $dlOk = -not $NoDownload
  if ($dlOk -and -not $Force -and (Test-Path $stamp)) {
    $age = (New-TimeSpan -Start (Get-Item $stamp).LastWriteTime -End (Get-Date)).TotalHours
    if ($age -ge 0 -and $age -lt $DownloadIntervalHours) { $dlOk = $false; Log ('download skipped: last attempt {0:N1} h ago' -f $age) }
  }
  if ($dlOk) { $a += '--download'; Set-Content -Path $stamp -Value (Get-Date -Format u) -Encoding ascii }
  if ($GpuDir) { New-Item -ItemType Directory -Force $GpuDir | Out-Null; $a += @('--deploy-gpu', $GpuDir) }
  if ($DriverDir) { New-Item -ItemType Directory -Force $DriverDir | Out-Null; $a += @('--deploy-drivers', $DriverDir) }
  Log "start: fwfetch $($a -join ' ')"
  $out = & $exe @a 2>&1 | Out-String
  $code = $LASTEXITCODE
  Log ($out.TrimEnd())
  Log "fwfetch exit code $code"
} else {
  Log "profile $Profile: all firmware files present and matching fw-lock.json, nothing to do"
}

# ---- 4. check again; only a clean state earns the marker -----------------------------------------------------------
$after = Test-Integrity
$bad = @($after.Cache) + @($after.Deployed) + @($after.Missing)
if ($null -eq $after.Lock) { $bad += [pscustomobject]@{ Id = 'lock'; Path = $lockPath; Why = 'missing'; Want = '' } }
foreach ($x in $bad) { Log "STILL BAD: $($x.Id) $($x.Why): $($x.Path)" }
if ($needFetch -and $code -eq 0 -and $bad.Count -eq 0) { Set-Content -Path $marker -Value (Get-Date -Format u) -Encoding ascii; Log 'complete: marker written' }
elseif (-not $needFetch -and $bad.Count -eq 0 -and -not (Test-Path $marker)) { Set-Content -Path $marker -Value (Get-Date -Format u) -Encoding ascii }
if ($bad.Count) { if ($code -eq 0) { $code = 2 }; Log 'the next start tries again' }
exit $code
