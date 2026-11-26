# Build info: HEG-001

- model: HEG-001
- commit: aff5ba80ecc9d4fa087e89921af5b10916dd7dae
- built: 2026-11-26 22:04:37Z
- WDK for most drivers: 10.0.19041.0 (a driver with its own wdk entry, e.g. Wi-Fi, uses that one)
- signed with: a new self-signed certificate made by this run

| driver | kit | result | note |
|---|---|---|---|
| swhid | agent/arm64 | OK |  |
| swbatt | agent/arm64 | OK |  |
| switchbt | agent/bt | OK (optional) |  |
| switchaudio | agent/audio | OK (optional) |  |
| switchwifi | agent/wifi | OK (optional) |  |

OK = built strict (/W4 /WX). WARN = built only with warnings allowed (newer clang). FAIL = no driver; see logs/.
**Test-signed only**: install SwitchWOA-Test.cer (Trusted Root + Trusted Publishers) and turn test signing on. Not verified on a real Switch by this build.
