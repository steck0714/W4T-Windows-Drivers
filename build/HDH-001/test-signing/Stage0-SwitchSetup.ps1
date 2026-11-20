<#
.SYNOPSIS
  Stage 0 on the Switch's Windows (run as Administrator, Windows PowerShell 5.1).

  -Mode Prepare : trust the SwitchWOA test certificate and turn on test signing
                  (bcdedit). Reboot afterwards.
  -Mode Hello   : load the SwoaHello test driver once (sc create/start), check
                  that it wrote its registry value, stop and remove it again.
  -Mode Status  : show test-signing / certificate / SwoaHello state only.

  Log: %SystemDrive%\SwitchWOA-Stage0.txt
#>
param([Parameter(Mandatory = $true)][ValidateSet('Prepare', 'Hello', 'Status')][string]$Mode)

$ErrorActionPreference = 'Continue'
# Windows PowerShell 5.1 on Windows 10 ARM64 is always x86 (there is no ARM64
# .NET Framework), so System32 is redirected to SysWOW64, which has no
# bcdedit.exe/pnputil.exe and its own drivers\ folder. Reach the real
# System32 through the Sysnative alias.
$Sys = Join-Path $env:windir 'Sysnative'
if (-not (Test-Path (Join-Path $Sys 'bcdedit.exe'))) { $Sys = Join-Path $env:windir 'System32' }
$Bcdedit = Join-Path $Sys 'bcdedit.exe'
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Log = "$env:SystemDrive\SwitchWOA-Stage0.txt"
function Log([string]$s) { Write-Host $s; Add-Content -Path $Log -Value $s -Encoding UTF8 }
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) { Write-Host 'Run this from an elevated (Administrator) PowerShell.'; exit 1 }
Log ''
Log "=== Stage0 $Mode  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')  process=$env:PROCESSOR_ARCHITECTURE 64bit=$([Environment]::Is64BitProcess) ps=$((Get-Process -Id $PID).Path)"

$Cer = Join-Path $Here 'SwitchWOA-Test.cer'
$Thumb = (New-Object Security.Cryptography.X509Certificates.X509Certificate2 $Cer).Thumbprint

# bcdedit fails with "The requested system device cannot be found" (and language packs / boot-file servicing fail
# for the same reason) when either
#   (a) the boot partition is not ACTIVE on an MBR disk (the kernel's SiValidateSystemPartition checks the boot
#       indicator; the partition type only matters on GPT), or
#   (b) the BCD is not marked as the system store: bcdboot writes Description\System and TreatAsSystem = 1, a BCD
#       built from BCD-Template lacks them and BiIsSystemStore rejects it.
# (a) can only be fixed from outside this Windows (set the boot indicator of partition 1 to 0x80). (b) is repaired
# here. Source: windows-switch v0.8.0 release notes and its agent\w64.cmd ":bcdfix" (found there by disassembling
# ntoskrnl.exe with Microsoft's public symbols); not verified on a Switch by this kit.
function Repair-BcdSystemStore {
  $null = & $Bcdedit /enum '{bootmgr}' 2>&1
  if ($LASTEXITCODE -eq 0) { Log 'bcd: opens fine, nothing to repair'; return $true }
  Log "bcd: bcdedit cannot open the store (exit=$LASTEXITCODE)"
  $mountvol = Join-Path $Sys 'mountvol.exe'; $reg = Join-Path $Sys 'reg.exe'
  $null = & $mountvol S: /S 2>&1
  if ($LASTEXITCODE -ne 0) {
    Log 'bcd: no EFI system partition is visible. Mark the boot partition (the FAT one that holds EFI\Microsoft\Boot, usually partition 1) ACTIVE from a PC: MBR byte 446 = 0x80. Only the boot indicator changes; hekate and the payload loaders ignore it. Then run this again.'
    return $false
  }
  try {
    $store = 'S:\EFI\Microsoft\Boot\BCD'
    if (Test-Path $store) {
      Log 'bcd: marking the store as the system store (Description\System, TreatAsSystem = 1)'
      $null = & $reg load 'HKLM\SWBCD' $store 2>&1; Log "  reg load exit=$LASTEXITCODE"
      if ($LASTEXITCODE -eq 0) {
        try {
          $null = & $reg add 'HKLM\SWBCD\Description' /v System /t REG_DWORD /d 1 /f 2>&1; Log "  System exit=$LASTEXITCODE"
          $null = & $reg add 'HKLM\SWBCD\Description' /v TreatAsSystem /t REG_DWORD /d 1 /f 2>&1; Log "  TreatAsSystem exit=$LASTEXITCODE"
        } finally { $null = & $reg unload 'HKLM\SWBCD' 2>&1; Log "  reg unload exit=$LASTEXITCODE" }
      }
    } else { Log "bcd: $store not found" }
  } finally { $null = & $mountvol S: /D 2>&1 }
  $null = & $Bcdedit /enum '{bootmgr}' 2>&1
  Log "bcd: after repair bcdedit exit=$LASTEXITCODE"
  return ($LASTEXITCODE -eq 0)
}

function Show-Status {
  $bcd = & $Bcdedit /enum '{current}' 2>&1
  Log ('bcdedit {current}: ' + (($bcd | Select-String -Pattern 'testsigning|nointegritychecks|path|description') -join ' ; '))
  foreach ($store in 'Root', 'TrustedPublisher') {
    $has = Test-Path "Cert:\LocalMachine\$store\$Thumb"
    Log "certificate in LocalMachine\$store : $has"
  }
  $sb = try { Confirm-SecureBootUEFI } catch { "n/a ($($_.Exception.Message))" }
  Log "SecureBoot: $sb  (native tools from $Sys)"
  Log ('sc query SwoaHello: ' + ((& sc.exe query SwoaHello 2>&1) -join ' ' -replace '\s+', ' '))
}

switch ($Mode) {
  'Prepare' {
    Log "certutil -addstore Root/TrustedPublisher $Cer"
    & certutil.exe -f -addstore Root $Cer | Out-Null; Log "  Root exit=$LASTEXITCODE"
    & certutil.exe -f -addstore TrustedPublisher $Cer | Out-Null; Log "  TrustedPublisher exit=$LASTEXITCODE"
    [void](Repair-BcdSystemStore)
    $out = & $Bcdedit /set '{current}' testsigning on 2>&1
    Log "bcdedit /set {current} testsigning on : $out (exit=$LASTEXITCODE)"
    Show-Status
    Log 'Next: restart Windows (Start > Power > Restart, or re-inject the payload), then run -Mode Hello.'
  }
  'Hello' {
    Show-Status
    $dst = Join-Path $Sys 'drivers\SwoaHello.sys'
    Copy-Item (Join-Path $Here 'SwoaHello.sys') $dst -Force
    $sig = Get-AuthenticodeSignature $dst
    Log "signature: $($sig.Status) $($sig.SignerCertificate.Subject)"
    & sc.exe delete SwoaHello 2>&1 | Out-Null
    Log ('sc create: ' + ((& sc.exe create SwoaHello type= kernel start= demand binPath= 'System32\drivers\SwoaHello.sys' 2>&1) -join ' '))
    Log ('sc start : ' + ((& sc.exe start SwoaHello 2>&1) -join ' ' -replace '\s+', ' '))
    Start-Sleep -Seconds 1
    $p = Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Services\SwoaHello\Parameters' -ErrorAction SilentlyContinue
    if ($p -and $p.Loaded -eq 1) { Log "RESULT: PASS  driver loaded, Loaded=$($p.Loaded) KmdfVersion=$($p.KmdfVersion)" }
    else { Log 'RESULT: FAIL  driver did not write Parameters\Loaded (see sc start above)' }
    Log ('sc stop  : ' + ((& sc.exe stop SwoaHello 2>&1) -join ' ' -replace '\s+', ' '))
    Log ('sc delete: ' + ((& sc.exe delete SwoaHello 2>&1) -join ' '))
    Remove-Item $dst -Force -ErrorAction SilentlyContinue
  }
  'Status' { Show-Status }
}
Log "Log: $Log"
