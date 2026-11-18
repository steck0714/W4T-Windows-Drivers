<#
.SYNOPSIS
  Collects the files this project must not redistribute (NVIDIA / Broadcom / Switchroot builds) from YOUR OWN
  Switch's SD card, the way Switchroot-based projects do, verifies them, and stages them OUTSIDE the repository.

.DESCRIPTION
  Read-only. Never writes to a disk or partition it reads from. Sources, in the order the manifest lists them:
    fs         files in a normal folder (the FAT32 'switchroot' folder, a mounted/copied rootfs, a kernel tree)
    initramfs  a file inside switchroot/*/ that is a (uImage-wrapped) gzip cpio archive; members are extracted in memory
    ext4       the Switchroot Linux partition read directly (raw disk or image), no mounting, no driver
    ihex       Intel-HEX firmware as it appears in the CTCaer kernel tree
  Where the manifest has a published SHA-256 / size the file must match it; otherwise it is accepted on first use and
  reported as UNVERIFIED (the hash is then pinned in fw-lock.json next to the staged files).

.PARAMETER Profile    aula (OLED, default) | lite | erista | all
.PARAMETER Auto       also look at every ready drive root, and (Windows, administrator) every physical disk for ext4 partitions
.PARAMETER SearchRoot extra folders to search (FAT32 'switchroot' parent, rootfs copy, kernel tree)
.PARAMETER DiskImage  raw disk / partition image files, or \\.\PhysicalDriveN, to scan for an ext4 partition (read-only)
.PARAMETER OutDir     staging directory. Refused if it is inside a git work tree.
.PARAMETER Deploy     copy the windows-switch files into this boot-partition root (wp11\bl31.bin, agent\wifi\..., ...)
.PARAMETER DeployGpu  copy the GPU firmware into this directory (e.g. C:\Windows\System32\drivers\TegraGpu)
.PARAMETER RequireKnownHash  refuse files that have no published hash
#>
[CmdletBinding()]
param(
  [ValidateSet('aula','lite','erista','all')][string]$Profile = 'aula',
  [string]$OutDir,
  [string[]]$SearchRoot = @(),
  [string[]]$DiskImage = @(),
  [switch]$Auto,
  [string]$Deploy,
  [string]$DeployGpu,
  [switch]$RequireKnownHash,
  [switch]$Quiet
)
$ErrorActionPreference = 'Stop'
$script:Here = Split-Path -Parent $MyInvocation.MyCommand.Path

function Say([string]$m) { if (-not $Quiet) { Write-Host $m } }

if (-not ('W4T.FwFetch.Ext4' -as [type])) { Add-Type -Path (Join-Path $script:Here 'FwFetch.cs') }

if (-not $OutDir) {
  $base = $env:LOCALAPPDATA; if (-not $base) { $base = Join-Path $HOME '.w4t' }
  $OutDir = Join-Path $base 'W4T/fw-cache'
}
$OutDir = [IO.Path]::GetFullPath($OutDir)

# Never stage proprietary files inside a repository.
$d = $OutDir
while ($d) {
  if (Test-Path -LiteralPath (Join-Path $d '.git')) { throw "Refusing to stage firmware inside a git work tree ($d). Use -OutDir outside the repository." }
  $parent = Split-Path -Parent $d; if ($parent -eq $d) { break }; $d = $parent
}

$manifest = Get-Content -Raw -LiteralPath (Join-Path $script:Here 'firmware-manifest.json') | ConvertFrom-Json

# ---- expand GPU shorthand entries -------------------------------------------------------------------------
$tpl = $manifest.gpu_template
$entries = @{}
foreach ($f in $manifest.files) {
  if ($f.PSObject.Properties.Name -contains 'gpu') {
    $paths = @(); foreach ($dir in $tpl.dirs) { $paths += ($dir + '/' + $f.gpu) }
    $e = [pscustomobject]@{ id = $f.id; what = $tpl.what + ' - ' + $f.gpu; owner = $tpl.owner; layout = $tpl.layout; gpuName = $f.gpu
      sources = @(
        [pscustomobject]@{ type = 'ext4'; paths = $paths; search_dir = $null },
        [pscustomobject]@{ type = 'fs';   paths = $paths }
      ) }
    $entries[$f.id] = $e
  } else { $entries[$f.id] = $f }
}
$ids = @()
if ($Profile -eq 'all') { $ids = @($entries.Keys | Sort-Object) }
else { $ids = @($manifest.profiles.$Profile) }

# ---- search roots -----------------------------------------------------------------------------------------
$roots = New-Object System.Collections.Generic.List[string]
foreach ($r in $SearchRoot) { if (Test-Path -LiteralPath $r -PathType Container) { $roots.Add([IO.Path]::GetFullPath($r)) } else { Write-Warning "SearchRoot not found: $r" } }
if ($Auto) {
  foreach ($di in [IO.DriveInfo]::GetDrives()) {
    try { if ($di.IsReady -and $di.DriveType -ne 'CDRom') { $roots.Add($di.RootDirectory.FullName) } } catch { }
  }
}
$roots = @($roots | Select-Object -Unique)

# ---- ext4 volumes (read-only) -----------------------------------------------------------------------------
$volumes = New-Object System.Collections.Generic.List[object]
function Add-Volumes([string]$path) {
  try {
    $fs = New-Object IO.FileStream($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
  } catch { Say "  (cannot open $path read-only: $($_.Exception.Message))"; return }
  $st = New-Object W4T.FwFetch.AlignedStream($fs, 4096)
  $found = 0
  $why = $null
  $whole = [W4T.FwFetch.Ext4]::TryOpen($st, 0, [ref]$why)
  if ($whole) { $volumes.Add([pscustomobject]@{ Label = "$path (whole)"; Fs = $whole }); $found++ }
  else {
    foreach ($p in [W4T.FwFetch.PartTable]::Read($st)) {
      $v = [W4T.FwFetch.Ext4]::TryOpen($st, $p.Offset, [ref]$why)
      if ($v) { $volumes.Add([pscustomobject]@{ Label = "$path partition $($p.Index)"; Fs = $v }); $found++ }
    }
  }
  Say "  $path : $found ext4 volume(s)"
}
foreach ($img in $DiskImage) { Add-Volumes $img }
if ($Auto -and $env:OS -eq 'Windows_NT') {
  $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
  if ($isAdmin) {
    try { foreach ($disk in Get-Disk) { Add-Volumes ('\\.\PhysicalDrive{0}' -f $disk.Number) } } catch { Say "  (Get-Disk failed: $($_.Exception.Message))" }
  } else { Say '  (not administrator: physical disks are not scanned for ext4; FAT32/folder sources still work)' }
}

# ---- helpers ----------------------------------------------------------------------------------------------
function Get-Sha256([byte[]]$b) {
  $h = [Security.Cryptography.SHA256]::Create()
  try { return ([BitConverter]::ToString($h.ComputeHash($b))).Replace('-', '').ToLowerInvariant() } finally { $h.Dispose() }
}
function Test-Expect($e, [byte[]]$b) {
  if ($null -eq $e) { return $true }
  if ($e.PSObject.Properties.Name -contains 'size' -and $b.Length -ne [int]$e.size) { return $false }
  if ($e.PSObject.Properties.Name -contains 'sha256' -and (Get-Sha256 $b) -ne $e.sha256) { return $false }
  return $true
}
function Get-Candidates($entry) {
  # yields @{ Bytes; Origin } objects lazily (list is small)
  foreach ($src in $entry.sources) {
    switch ($src.type) {
      'fs' {
        foreach ($root in $roots) { foreach ($p in $src.paths) {
          $hits = @(Get-ChildItem -Path (Join-Path $root $p) -File -ErrorAction SilentlyContinue)
          foreach ($h in $hits) { if ($h.Length -le 64MB) { [pscustomobject]@{ Bytes = [IO.File]::ReadAllBytes($h.FullName); Origin = "file $($h.FullName)" } } }
        } }
      }
      'initramfs' {
        foreach ($root in $roots) { foreach ($p in $src.scan) {
          foreach ($h in @(Get-ChildItem -Path (Join-Path $root $p) -File -ErrorAction SilentlyContinue)) {
            if ($h.Length -lt 16KB -or $h.Length -gt 512MB) { continue }
            if ($h.Extension -match '^\.(7z|zip|bmp|ini|dtb|dtimg|scr|txt|md)$') { continue }
            $why = $null
            $b = [W4T.FwFetch.Initramfs]::TryExtract($h.FullName, [string[]]@($src.member), 16MB, [ref]$why)
            if ($b) { [pscustomobject]@{ Bytes = $b; Origin = "initramfs $($h.FullName) :: $($src.member)" } }
            elseif ($why -and $why -notmatch 'not an initramfs|too small|member not found') { Say "  skip $($h.Name): $why" }
          }
        } }
      }
      'ext4' {
        foreach ($v in $volumes) {
          $tried = @{}
          foreach ($p in $src.paths) {
            $b = $null
            try { $b = $v.Fs.ReadFile($p, 64MB) } catch { Say "  ext4 read failed ($p): $($_.Exception.Message)" }
            $tried[$p.Split('/')[-1]] = 1
            if ($b) { [pscustomobject]@{ Bytes = $b; Origin = "ext4 $($v.Label) :: /$p" } }
          }
          if ($src.search_dir -and $entry.PSObject.Properties.Name -contains 'expect' -and $entry.expect.PSObject.Properties.Name -contains 'sha256') {
            foreach ($n in $v.Fs.ListNames($src.search_dir)) {
              if ($tried.ContainsKey($n)) { continue }
              $b = $null; try { $b = $v.Fs.ReadFile($src.search_dir + '/' + $n, 64MB) } catch { }
              if ($b) { [pscustomobject]@{ Bytes = $b; Origin = "ext4 $($v.Label) :: /$($src.search_dir)/$n (found by hash)" } }
            }
          }
        }
      }
      'ihex' {
        foreach ($root in $roots) { foreach ($p in $src.paths) {
          foreach ($h in @(Get-ChildItem -Path (Join-Path $root $p) -File -ErrorAction SilentlyContinue)) {
            try { $b = [W4T.FwFetch.Ihex]::ToBin([IO.File]::ReadAllText($h.FullName)); [pscustomobject]@{ Bytes = $b; Origin = "ihex $($h.FullName)" } }
            catch { Say "  skip $($h.Name): $($_.Exception.Message)" }
          }
        } }
      }
    }
  }
}
function Set-RandomMac([byte[]]$b, [byte[]]$existing) {
  $text = [Text.Encoding]::ASCII.GetString($b)
  $mac = $null
  if ($existing) { $m = [regex]::Match([Text.Encoding]::ASCII.GetString($existing), '(?m)^macaddr=([0-9A-Fa-f:]{17})'); if ($m.Success) { $mac = $m.Groups[1].Value } }
  if (-not $mac) {
    $r = New-Object byte[] 6; $rng = [Security.Cryptography.RandomNumberGenerator]::Create(); $rng.GetBytes($r); $rng.Dispose()
    $r[0] = ($r[0] -band 0xFC) -bor 0x02
    $mac = (($r | ForEach-Object { $_.ToString('x2') }) -join ':')
  }
  if ($text -notmatch '(?m)^macaddr=') { Write-Warning 'board file has no macaddr= line; left unchanged'; return $b }
  $text = [regex]::Replace($text, '(?m)^macaddr=.*?(\r?)$', ('macaddr=' + $mac + '$1'))
  return [Text.Encoding]::ASCII.GetBytes($text)
}

# ---- main -------------------------------------------------------------------------------------------------
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$lockPath = Join-Path $OutDir 'fw-lock.json'
$lock = @{}
if (Test-Path -LiteralPath $lockPath) { (Get-Content -Raw -LiteralPath $lockPath | ConvertFrom-Json).PSObject.Properties | ForEach-Object { $lock[$_.Name] = $_.Value } }

Say ("fwfetch profile={0}  roots={1}  ext4 volumes={2}  staging={3}" -f $Profile, $roots.Count, $volumes.Count, $OutDir)
$report = @(); $missing = 0
foreach ($id in $ids) {
  $e = $entries[$id]
  if (-not $e) { Write-Warning "manifest has no entry '$id'"; continue }
  $hasExpect = $e.PSObject.Properties.Name -contains 'expect'
  $hasHash = $hasExpect -and ($e.expect.PSObject.Properties.Name -contains 'sha256')
  $got = $null
  if ($RequireKnownHash -and -not $hasHash) { Say "[skip ] $id : no published hash and -RequireKnownHash"; $report += [pscustomobject]@{ Id = $id; Status = 'REFUSED'; Origin = '' }; $missing++; continue }
  foreach ($c in (Get-Candidates $e)) {
    $exp = $null; if ($hasExpect) { $exp = $e.expect }
    if (Test-Expect $exp $c.Bytes) { $got = $c; break }
    else { Say "  reject $($c.Origin): size/hash differs from the published value" }
  }
  if (-not $got) { Say "[MISS ] $id : not found ($($e.what))"; $report += [pscustomobject]@{ Id = $id; Status = 'MISSING'; Origin = '' }; $missing++; continue }
  $bytes = $got.Bytes
  $rel = if ($e.PSObject.Properties.Name -contains 'gpuName') { 'gpu/' + $e.gpuName } else { $e.deploy.'windows-switch'[0] }
  $dest = Join-Path $OutDir $rel
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dest) | Out-Null
  if ($e.PSObject.Properties.Name -contains 'postprocess' -and $e.postprocess -eq 'random_macaddr') {
    $old = $null; if (Test-Path -LiteralPath $dest) { $old = [IO.File]::ReadAllBytes($dest) }
    $bytes = Set-RandomMac $bytes $old
  }
  [IO.File]::WriteAllBytes($dest, $bytes)
  $sha = Get-Sha256 $bytes
  $status = if ($hasHash) { 'OK (hash matches published value)' } else { 'UNVERIFIED (accepted on first use)' }
  if ($lock.ContainsKey($id) -and -not $hasHash -and $lock[$id].sha256 -ne $sha -and -not ($e.PSObject.Properties.Name -contains 'postprocess')) {
    Write-Warning "$id differs from the copy pinned in fw-lock.json ($($lock[$id].sha256.Substring(0,12)) -> $($sha.Substring(0,12))). Different firmware version?"
  }
  $lock[$id] = [pscustomobject]@{ sha256 = $sha; size = $bytes.Length; origin = $got.Origin; staged = $rel; verified = $hasHash; utc = [DateTime]::UtcNow.ToString('o') }
  Say ("[{0}] {1} : {2} bytes  {3}`n        from {4}" -f $(if ($hasHash) { 'OK   ' } else { 'WARN ' }), $id, $bytes.Length, $status, $got.Origin)
  $report += [pscustomobject]@{ Id = $id; Status = $status; Origin = $got.Origin }
}
($lock | ConvertTo-Json -Depth 4) | Set-Content -LiteralPath $lockPath -Encoding UTF8

# ---- deploy -----------------------------------------------------------------------------------------------
if ($Deploy) {
  foreach ($id in $ids) {
    $e = $entries[$id]; if (-not $lock.ContainsKey($id) -or $e.PSObject.Properties.Name -contains 'gpuName') { continue }
    foreach ($t in $e.deploy.'windows-switch') {
      $to = Join-Path $Deploy $t; New-Item -ItemType Directory -Force -Path (Split-Path -Parent $to) | Out-Null
      Copy-Item -LiteralPath (Join-Path $OutDir $lock[$id].staged) -Destination $to -Force; Say "deployed $t"
    }
  }
}
if ($DeployGpu) {
  New-Item -ItemType Directory -Force -Path $DeployGpu | Out-Null
  foreach ($id in $ids) {
    $e = $entries[$id]; if (-not $lock.ContainsKey($id) -or -not ($e.PSObject.Properties.Name -contains 'gpuName')) { continue }
    Copy-Item -LiteralPath (Join-Path $OutDir $lock[$id].staged) -Destination (Join-Path $DeployGpu $e.gpuName) -Force; Say "deployed GPU $($e.gpuName)"
  }
}

Say ''
Say ("{0} of {1} found. Staged copies and fw-lock.json are in {2}" -f ($ids.Count - $missing), $ids.Count, $OutDir)
if ($missing -gt 0) { exit 2 }
