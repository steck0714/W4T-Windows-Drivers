<#
.SYNOPSIS
  Update the SwitchWOA drivers on the Switch's Windows from a new build (run as Administrator, Windows PowerShell 5.1).

  1. Trusts the .cer of THIS build when it is not trusted yet (Root + TrustedPublisher). A build made with the repository's
     stable certificate (secrets SWOA_PFX_BASE64 / SWOA_PFX_PASSWORD) has the same .cer every time, so this is a no-op;
     a build with a per-run certificate is trusted automatically here, so updating never needs a manual certificate step.
  2. Installs every driver package (INF) of the build with pnputil. Windows replaces an installed driver only when the new
     DriverVer is higher; CI stamps a growing DriverVer into every build (tools/ci/stamp_driver_ver.py).
  3. Writes what it did to C:\ProgramData\W4T\logs\update-drivers.log. Reboot afterwards when it says so.

  -KitDir   folder that holds the build (the driver folders with INF/SYS/CAT); default: the folder above this script
  -Cer      the build's SwitchWOA-Test.cer; default: found next to this script or in -KitDir
  -CheckOnly   list what would be installed (installed vs new DriverVer), change nothing
  Test signing must already be on (Install-TestSigning.cmd once). Firmware is NOT part of a driver update: fwfetch keeps it.
#>
param(
  [string]$KitDir = (Split-Path -Parent $PSScriptRoot),
  [string]$Cer = '',
  [switch]$CheckOnly
)
$ErrorActionPreference = 'Continue'
$Sys = Join-Path $env:windir 'Sysnative'      # 32-bit PowerShell on ARM64: reach the real System32
if (-not (Test-Path (Join-Path $Sys 'pnputil.exe'))) { $Sys = Join-Path $env:windir 'System32' }
$Pnp = Join-Path $Sys 'pnputil.exe'
$logDir = Join-Path $env:ProgramData 'W4T\logs'; New-Item -ItemType Directory -Force $logDir | Out-Null
$Log = Join-Path $logDir 'update-drivers.log'
function Log([string]$s) { Write-Host $s; Add-Content -Path $Log -Value ("{0:u} {1}" -f (Get-Date), $s) -Encoding UTF8 }
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { Write-Host 'Run this from an elevated (Administrator) PowerShell.'; exit 1 }

if (-not $Cer) {
  $Cer = @((Join-Path $PSScriptRoot 'SwitchWOA-Test.cer'), (Join-Path $KitDir 'SwitchWOA-Test.cer'), (Join-Path $KitDir 'test-signing\SwitchWOA-Test.cer')) | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $CheckOnly) {
  if (-not $Cer -or -not (Test-Path $Cer)) { Log 'no SwitchWOA-Test.cer found: put it next to this script or pass -Cer (needed to trust the signature)'; exit 1 }
  $c = New-Object Security.Cryptography.X509Certificates.X509Certificate2 $Cer
  foreach ($store in 'Root', 'TrustedPublisher') {
    $s = New-Object Security.Cryptography.X509Certificates.X509Store $store, 'LocalMachine'
    $s.Open('ReadWrite')
    if ($s.Certificates | Where-Object { $_.Thumbprint -eq $c.Thumbprint }) { Log "certificate $($c.Thumbprint) already trusted in $store" }
    else { $s.Add($c); Log "certificate $($c.Thumbprint) ($($c.Subject)) added to $store" }
    $s.Close()
  }
  if ((& (Join-Path $Sys 'bcdedit.exe') /enum '{current}' 2>$null | Out-String) -notmatch 'testsigning\s+Yes') { Log 'WARNING: test signing is off. Run Install-TestSigning.cmd once and reboot, or the new drivers will not load.' }
}

$infs = Get-ChildItem $KitDir -Recurse -Filter *.inf -ErrorAction SilentlyContinue | Where-Object { $_.FullName -notmatch '\\(test-signing|fwfetch)\\' }
if (-not $infs) { Log "no INF files under $KitDir"; exit 1 }
$fail = 0; $changed = 0
foreach ($f in $infs) {
  $ver = (Select-String -Path $f.FullName -Pattern '^\s*DriverVer\s*=\s*(.+)$' -List | ForEach-Object { $_.Matches[0].Groups[1].Value.Trim() })
  $sys = (Get-Content $f.FullName | Where-Object { $_ -match '\.sys' } | Select-Object -First 1)
  if ($CheckOnly) { Log "would install $($f.Name) DriverVer $ver"; continue }
  $out = & $Pnp /add-driver $f.FullName /install 2>&1 | Out-String
  $code = $LASTEXITCODE
  # 0 = installed / updated, 3010 = ok but reboot, 259 (NO_MORE_ITEMS) = nothing newer to install
  if ($code -eq 0 -or $code -eq 3010) { $changed++; Log "OK   $($f.Name) $ver" + $(if ($code -eq 3010) { ' (reboot needed)' } else { '' }) }
  elseif ($out -match 'already|up to date|newer') { Log "SAME $($f.Name) $ver (the installed driver is already this version or newer)" }
  else { $fail++; Log "FAIL $($f.Name) $ver exit=$code : $($out.Trim() -replace '\s+', ' ')" }
}
if (-not $CheckOnly) { Log "done: $changed installed/updated, $fail failed. Reboot to load updated drivers." }
exit $(if ($fail) { 1 } else { 0 })
