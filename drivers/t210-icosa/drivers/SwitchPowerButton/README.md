# SwitchPowerButton (ACPI\SWOA0601)

1.0.0.2 adds an optional hold action ported from windows-switch's `swhid`. Everything is off by
default; with no registry value the driver behaves exactly like 1.0.0.1.

| Registry (`HKLM\SYSTEM\CurrentControlSet\Services\switchpwrbtn\Parameters`) | Meaning |
|---|---|
| `HoldSasMs` (DWORD) = 0 / absent | off: every press/release is reported at once as System Power Down |
| `HoldSasMs` = 1000..6000 | a press shorter than this is reported on **release**; a press held this long sends **Ctrl+Alt+Del** (keyboard collection) and no power action for that press |

Set it, then disable/enable the device (or reboot):

    reg add HKLM\SYSTEM\CurrentControlSet\Services\switchpwrbtn\Parameters /v HoldSasMs /t REG_DWORD /d 4000 /f

Notes
- The keyboard collection exists only when `HoldSasMs` is non-zero. Windows may treat a hardware
  keyboard as present and stop auto-showing the touch keyboard; that is the reason it is opt-in.
- The power action moves from press to release in that mode, so it is slightly later.
- The MAX77620 still powers off by itself on a long hold (after several seconds), as before.
- The edge logic is in `src/hold_sas.h`; `tests/test_hold_sas.c` runs on any host (`tools/run_host_tests.sh`).
- **Not built with the WDK and not run on a console.** Only the host test and a stubbed syntax check were done.
- Lite (t214) has its own 1.0.0.5 driver (wake/IRQ state machine); this change is not applied there.

## Why the upper limit is 6000 ms
Microsoft documents that Windows recognises a power-button hold at about 7 s and starts a forced
shutdown, with firmware cutting power at about 10 s. Values above 6000 are clamped so the Ctrl+Alt+Del
happens before that. In hold mode Windows only sees the power press on release, so for a long hold it
does not see the press at all; the PMIC's own hardware power-off after a long hold still applies.

## Cross-checks done (2026-11-16)
- MAX77620 `ONOFFIRQ` 0x0B, `ONOFFSTAT` 0x15: match the mainline Linux `max77620.h` and winx's `Max77620.h`.
- Bit layout (ONOFFSTAT.EN0 = 0x04, ONOFFIRQ EN0_F = 0x04 / EN0_R = 0x08) could **not** be confirmed from any public
  source reachable here (Linux/NetBSD headers omit it; hekate's header was not reachable). It rests on the
  existing winx 1.0.0.1 driver, swhid (`1<<2`) and the Stage 3 self-test, which are field-tested.
- HID: Power Down is Generic Desktop 0x81 (System Control collection 0x80) per Microsoft; Windows on tablets maps
  Power+Volume-Down to Ctrl+Alt+Del natively (Win10+), so `HoldSasMs` is only needed where that chord is unavailable.
