<#
.SYNOPSIS
  Build every driver of one Switch model listed in its drivers-*.json with that family's tools\Build-Driver.ps1,
  test-sign them all with ONE certificate, and assemble the kits. Runs the same on a GitHub windows-2022 runner and on your PC.

  The model comes from build\models.json (-Model HAC-001 | HDH-001): that file names the driver list, the folder the
  sources are in, the family's tools folder (Build-Driver.ps1, kmdf_support.c, test certificate) and the kit sources.
  Without -Model the script behaves as before: Erista (t210-icosa), output build\t210.

  Per driver: first strict (/W4 /WX like the local build); if only a newer clang's warnings fail it, retry with
  -NoWarnAsError and mark it WARN. A hard failure is FAIL (the script exits 1 at the end, after trying everything).
  Drivers marked "optional" in the list never make the run fail.

.EXAMPLE
  .\tools\ci\Build-AllDrivers.ps1 -Model HDH-001                 # Switch Lite -> build\_work\HDH-001
  .\tools\ci\Build-AllDrivers.ps1 -Model HAC-001 -Only switchfan,switchbattery
  .\tools\ci\Build-AllDrivers.ps1                                # Erista, as before -> build\t210
#>
param(
  [string]$Model = '',
  [string]$Config = '',
  [string]$OutRoot = '',
  [string[]]$Only = @(),
  [switch]$NoSign
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
function Resolve-Rel([string]$p) { Join-Path $Root ($p -replace '/', '\') }

if ($Model) {
  $all = (Get-Content (Join-Path $Root 'build\models.json') -Raw | ConvertFrom-Json).models
  $prop = $all.PSObject.Properties[$Model]
  if (-not $prop) { throw "unknown model '$Model' (have: $(($all.PSObject.Properties.Name) -join ', '))" }
  $m = $prop.Value
  if ($m.build -ne 'ci') { throw "model $Model is not built here (build=$($m.build), status=$($m.status)): $($m.note)" }
  if (-not $Config)  { $Config  = Resolve-Rel $m.ci_config }
  $DriverRoot = Resolve-Rel $m.driver_root
  $ToolsDir   = Resolve-Rel $m.tools_dir
  $KitsDir    = Resolve-Rel $m.kits_dir
  if (-not $OutRoot) { $OutRoot = Join-Path $Root "build\_work\$Model" }
} else {
  $Model      = 'HAC-001'
  if (-not $Config)  { $Config  = "$PSScriptRoot\drivers-t210.json" }
  $DriverRoot = Join-Path $Root 'drivers\t210-icosa\drivers'
  $ToolsDir   = Join-Path $Root 'drivers\t210-icosa\tools'
  $KitsDir    = Join-Path $Root 'drivers\t210-icosa\kits'
  if (-not $OutRoot) { $OutRoot = Join-Path (Get-Location) 'build\t210' }
}
$Build = Join-Path $ToolsDir 'Build-Driver.ps1'
if (-not (Test-Path $Build))  { throw "missing $Build" }
if (-not (Test-Path $Config)) { throw "missing $Config" }
$Kits  = "${env:ProgramFiles(x86)}\Windows Kits\10"
$Default = if ($env:SWOA_FALLBACK_WDK) { '10.0.26100.0' } else { '10.0.19041.0' }
$list = @(Get-Content $Config -Raw | ConvertFrom-Json)
if ($Only.Count) { $list = @($list | Where-Object { $Only -contains $_.name }) }

New-Item -ItemType Directory -Force $OutRoot | Out-Null
$LogDir = Join-Path $OutRoot 'logs'; New-Item -ItemType Directory -Force $LogDir | Out-Null
$KitRoot = Join-Path $OutRoot 'kit'; New-Item -ItemType Directory -Force $KitRoot | Out-Null
$results = @()

function Invoke-One($d, [bool]$relaxed, [string]$log) {
  $wdk = if ($d.wdk) { $d.wdk } else { $Default }
  $kmlib = "$Kits\Lib\$wdk\km\arm64"
  $inc = "$Kits\Include\$wdk"
  $dir = Join-Path $DriverRoot $d.dir
  $p = @{
    Name = $d.name; Wdk = $wdk
    OutDir = (Join-Path $OutRoot "work\$($d.name)")
    Sources = @($d.sources | ForEach-Object { Join-Path $dir $_ })
  }
  if ($d.inf)      { $p.Inf = Join-Path $dir $d.inf }
  if ($d.libs)     { $p.Libs = @($d.libs | ForEach-Object { $_ -replace '\{KMLIB\}', $kmlib }) }
  if ($d.wdm)      { $p.Wdm = $true }
  if ($d.kmdf)     { $p.KmdfVersion = $d.kmdf }
  if ($d.ntddi)    { $p.Ntddi = $d.ntddi }
  if ($d.defines)  { $p.Defines = @($d.defines) }
  # the SPB headers live under km\spb\1.0 (harmless for drivers that do not need them); WiFiCx adds its own
  $extra = @('km\spb\1.0') + @($d.includes)
  $p.ExtraIncludes = @($extra | ForEach-Object { Join-Path $inc $_ } | Where-Object { Test-Path $_ })
  if ($NoSign)     { $p.NoSign = $true }
  if ($relaxed)    { $p.NoWarnAsError = $true }
  Start-Transcript -Path $log -Force | Out-Null
  try { & $Build @p } finally { Stop-Transcript | Out-Null }
}

foreach ($d in $list) {
  Write-Host "::group::$($d.name)"
  $status = 'OK'; $note = ''
  $log = Join-Path $LogDir "$($d.name).log"
  try { Invoke-One $d $false $log }
  catch {
    $first = $_.Exception.Message
    try { Invoke-One $d $true "$log.relaxed.txt"; $status = 'WARN'; $note = "builds only without /WX ($first)" }
    catch { $status = 'FAIL'; $note = $_.Exception.Message }
  }
  Write-Host "$($d.name): $status $note"
  Write-Host '::endgroup::'
  $results += [pscustomobject]@{ Name = $d.name; Kit = $d.kit; Status = $status; Optional = [bool]$d.optional; Note = $note }
  if ($status -eq 'FAIL' -and $d.optional) { Write-Host "::warning::optional driver $($d.name) failed: $note" }
  elseif ($status -eq 'FAIL') { Write-Host "::error::$($d.name) failed: $note" }
  elseif ($status -eq 'WARN') { Write-Host "::warning::$($d.name): $note" }
}

# --- assemble the kits: built files + the kit's own scripts/README -----------------------------------------------
foreach ($r in ($results | Where-Object { $_.Status -ne 'FAIL' })) {
  $k = Join-Path $KitRoot $r.Kit; New-Item -ItemType Directory -Force $k | Out-Null
  $srcKit = Join-Path $KitsDir $r.Kit
  if (Test-Path $srcKit) { Copy-Item "$srcKit\*" $k -Recurse -Force }
  Get-ChildItem (Join-Path $OutRoot "work\$($r.Name)") -File | Where-Object { $_.Extension -in '.sys', '.inf', '.cat' } |
    ForEach-Object { Copy-Item $_.FullName $k -Force }
}
$cer = Join-Path $ToolsDir 'cert\SwitchWOA-Test.cer'
if (Test-Path $cer) { Copy-Item $cer $KitRoot -Force }
# A kit folder may carry an OLD SwitchWOA-Test.cer from the repository (Lite's Switch-Stage0 does). That is not the certificate
# this run signed with: overwrite every such copy with the fresh one, or Stage0 would trust the wrong certificate.
if (Test-Path $cer) {
  Get-ChildItem $KitRoot -Recurse -Filter 'SwitchWOA-Test.cer' | Where-Object { $_.DirectoryName -ne $KitRoot } |
    ForEach-Object { Copy-Item $cer $_.FullName -Force; Write-Host "refreshed stale certificate copy: $($_.FullName.Substring($KitRoot.Length + 1))" }
}
$cerNote = if ($env:SWOA_CERT_THUMBPRINT) { "the certificate from the SWOA_PFX_BASE64 secret (thumbprint $($env:SWOA_CERT_THUMBPRINT))" } else { 'a new self-signed certificate made by this run' }

$sums = Get-ChildItem $KitRoot -Recurse -File | Where-Object { $_.Extension -in '.sys', '.inf', '.cat', '.cer' } | ForEach-Object {
  '{0}  {1}' -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower(), $_.FullName.Substring($KitRoot.Length + 1).Replace('\', '/')
}
Set-Content (Join-Path $KitRoot 'SHA256SUMS.txt') $sums -Encoding ascii

$sha = if ($env:GITHUB_SHA) { $env:GITHUB_SHA } else { (git -C $Root rev-parse HEAD 2>$null) }
$md = @("# Build info: $Model", "", "- model: $Model", "- commit: $sha", "- built: $((Get-Date).ToUniversalTime().ToString('u'))", "- WDK for most drivers: $Default (a driver with its own wdk entry, e.g. Wi-Fi, uses that one)",
        "- signed with: $cerNote", "", "| driver | kit | result | note |", "|---|---|---|---|")
foreach ($r in $results) { $md += "| $($r.Name) | $($r.Kit) | $($r.Status)$(if ($r.Optional) { ' (optional)' }) | $($r.Note -replace '\|', '/') |" }
$md += @("", "OK = built strict (/W4 /WX). WARN = built only with warnings allowed (newer clang). FAIL = no driver; see logs/.",
         "**Test-signed only**: install SwitchWOA-Test.cer (Trusted Root + Trusted Publishers) and turn test signing on. Not verified on a real Switch by this build.")
Set-Content (Join-Path $OutRoot 'BUILD-INFO.md') $md -Encoding utf8
Copy-Item (Join-Path $OutRoot 'BUILD-INFO.md') $KitRoot -Force

ConvertTo-Json -InputObject @($results) | Set-Content (Join-Path $OutRoot 'results.json') -Encoding utf8
$results | Format-Table Name, Status, Optional, Note -AutoSize | Out-String | Write-Host
$hard = @($results | Where-Object { $_.Status -eq 'FAIL' -and -not $_.Optional })
if ($hard.Count) { Write-Host "FAILED: $($hard.Name -join ', ')"; exit 1 }
