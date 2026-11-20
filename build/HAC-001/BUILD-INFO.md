# Build info: HAC-001

- model: HAC-001
- commit: 858daf245b12661614ef029af96153ee3e646aad
- built: 2026-11-20 05:56:51Z
- WDK for most drivers: 10.0.19041.0 (a driver with its own wdk entry, e.g. Wi-Fi, uses that one)
- signed with: a new self-signed certificate made by this run

| driver | kit | result | note |
|---|---|---|---|
| swoahello | Switch-Stage0 | OK |  |
| tegra210gpio | Switch-Stage1 | OK |  |
| tegra210i2c | Switch-Stage3 | OK |  |
| switchfan | Switch-Fan | OK |  |
| switchcpu | Switch-CpuClock | OK |  |
| switchbattery | Switch-Battery | OK |  |
| switchpwrbtn | Switch-PowerButton | OK |  |
| switchrtc | Switch-Rtc | OK |  |
| switchsensor | Switch-Sensor | OK |  |
| switchtouch | Switch-Touch | OK |  |
| switchbt | Switch-Bluetooth | OK |  |
| nvtsdhc | Switch-Sd | OK |  |
| switchaudio | Switch-Audio | OK |  |
| swoadiag | Switch-UsbDiag | OK |  |
| switchwprobe | Switch-Wifi-Probe | OK (optional) |  |
| switchwlan | Switch-Wlan | OK |  |

OK = built strict (/W4 /WX). WARN = built only with warnings allowed (newer clang). FAIL = no driver; see logs/.
**Test-signed only**: install SwitchWOA-Test.cer (Trusted Root + Trusted Publishers) and turn test signing on. Not verified on a real Switch by this build.
