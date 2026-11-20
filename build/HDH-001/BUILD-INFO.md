# Build info: HDH-001

- model: HDH-001
- commit: 79ad434f7fc9aee6ca6f6e5b94c062fdc0015327
- built: 2026-11-20 04:03:02Z
- WDK for most drivers: 10.0.19041.0 (a driver with its own wdk entry, e.g. Wi-Fi, uses that one)
- signed with: a new self-signed certificate made by this run

| driver | kit | result | note |
|---|---|---|---|
| swoahello | Switch-Stage0 | OK (optional) |  |
| tegra210gpio | Switch-Stage1 | OK |  |
| tegra210i2c | Switch-Stage3 | OK |  |
| switchtouch | Switch-Touch | OK |  |
| switchpwrbtn | Switch-PowerButton | OK |  |
| switchfan | Switch-Fan | OK |  |
| switchbattery | Switch-Battery | OK |  |
| switchrtc | Switch-Rtc | OK |  |
| switchbt | Switch-Bluetooth | OK |  |
| switchsio | Switch-Sensor | OK |  |
| tegraxusb | Switch-Xusb | OK |  |
| nvtsdhc | Switch-Sd | OK |  |
| switchaudio | Switch-Audio | OK |  |
| swoadiag | Switch-UsbDiag | OK (optional) |  |
| switchcpu | Switch-CpuClock | OK (optional) |  |
| switchwifi | Switch-Wifi | OK (optional) |  |

OK = built strict (/W4 /WX). WARN = built only with warnings allowed (newer clang). FAIL = no driver; see logs/.
**Test-signed only**: install SwitchWOA-Test.cer (Trusted Root + Trusted Publishers) and turn test signing on. Not verified on a real Switch by this build.
