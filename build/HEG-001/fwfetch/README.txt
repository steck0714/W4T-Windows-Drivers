fwfetch reads firmware that must not be redistributed from YOUR OWN SD card (read-only, no network).
HEG-001 (windows-switch layout): put the built agent\ folder (drivers\agent) on the boot partition, then run once, in an administrator
PowerShell on a PC with the SD card, or on the Switch with the boot partition mounted (E: = the boot partition: use YOUR letter, see Get-Volume; if it has none, pass its \\?\Volume{GUID}\ path):
  .\fwfetch.exe --profile aula --auto --deploy E:\
This puts bl31.bin / the XUSB firmware into wp11\ and wp64\ and the BCM4356 firmware / patch into agent\wifi\ and agent\bt\.
Install-FwFetchStartup.ps1 -Profile aula only keeps a verified copy in %ProgramData%\W4T\fw-cache (optional).
