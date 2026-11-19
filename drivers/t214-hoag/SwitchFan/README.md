# Switch Lite fan, CPU and memory control

**Fan33/Core7/Battery14 is installed and accepted by the owner after manual testing.**
Fan33 adds Noble ACTMON watermark interrupts and the actual Lite CPU→EMC
bandwidth-floor table. Native readiness confirmed handled IRQs, completed EMC
transitions and zero EMC/ACTMON/QoS errors. Workload duration and exhaustive
frequency coverage were not reported; no such stress acceptance is claimed.
[Current profiles, tool behavior and acceptance table](../../docs/hdh001/dvfs-tray.md).

| Function | Current behavior |
|---|---|
| CPU/GPU modes | Seven captured Noble profiles; real DFLL/GPCCLK readbacks, thermal bounds and failed-transition handling |
| RAM | Installed Fan33: IRQ demand + CPU/display floors,204MHz minimum, BPMP-trained204/1331.2/1600/2500MHz points |
| Fan | Independent Console / Handheld / Cool; first-use default Handheld; app restores last confirmed selection |
| Thermal input | Maximum TMP451 SoC/board temperature; continuous smoothing; retained CPU cooling floor and full PWM on sensor loss / raw80°C emergency |
| Persistence | Power and fan selections are independently saved after confirmation; normal reboot/logon restores them |

The new curve data is from the same-machine Noble DT, with Linux continuous
thermal-governor math adapted under its original GPL notices. Linux's skin
estimator is not ported; Windows temperature input and protection floors differ.
PWM is not measured fan RPM. Existing version13 evidence below is historical.

## Historical version13 evidence

Historical version **1.0.0.13** lowers the Lite runtime CPU target to **1497600 kHz**
(**1497.6 MHz**), as requested by the owner. `CpuRequestedKhz` reports
that target; the new `CpuHandoffKhz` retains the firmware's original 1996800-kHz
request. The firmware handoff format and RAM settings are unchanged. The driver
keeps the previously verified 1120-mV CPU rail setting, the fan curve below,
and the existing stock-clock restoration and thermal hysteresis. Host tests
cover the new target, rail/clock ordering, thermal fallback and screen-off
behavior.

After the 2026-11-12 17:48 PST restart (2026-11-13 01:48 UTC), the physical
Lite selected `oem91.inf`, runtime `DriverVersion=13`, and `switchfan.sys`
SHA-256 `d91005928482b97a99e4f950e55b6822203e9735d313bd0464823d96935f69ed`.
Both `CpuRequestedKhz` and the PLL readback `CpuActualKhz` were **1497600**;
`CpuLastError=0`, `CpuMv=1120`, and `CpuHandoffKhz=1996800`. At 37 C the
fan requested 76/236 PWM. Display state was on with brightness/backlight duty
100. RAM requests, the other fan settings and `powercfg` settings matched
the saved baseline. These are controller readbacks, not a physical RPM or
display-image measurement.

Previous verified version: **1.0.0.12**. On 2026-11-12, SSH confirmed the physical Lite
selected `oem86.inf`, runtime `DriverVersion=12`, and `switchfan.sys` SHA-256
`6a1c51a93ef0521482cff45bdb74442dd4aa22711f525adbede603dfc8e896ca`.

| SoC temperature | Requested PWM |
| --- | --- |
| 35 C | 32% (76/236) |
| 38 C | 50% (118/236) |
| 40 C | 75% (177/236) |
| 43 C | 90% (212/236) |
| 45 C and above | 100% (236/236) |

At 33–34 C the previous duty is retained, capped at 76/236; at 32 C or below
the thermal curve requests zero. The existing CPU-overclock cooling floor can
keep the fan on below those thresholds. Failed sensor readings retain fail-safe
cooling. These are PWM requests; fan RPM has not been measured.

On that earlier build, `CpuOverclockEnabled=1` was restored on the device and
the driver read back **1996800 kHz** after its device restart. Thermal throttling,
the screen-off clock restoration policy and system sleep settings were retained.
Host controls covering actual fan callbacks and T214 clock ordering pass; a
physical temperature sweep across every threshold has not been performed.

The optional GPU PMIC bridge is maintained only in
[mesa-switch-woa](https://github.com/ruje0504-2/mesa-switch-woa/tree/tgd3d-vulkan/tegra-x1-wddm/integration/switch-lite/windows/SwitchFan).
The local `src/t214/gpu_pmic.h` forwards to that implementation. Builds defining
`SWITCH_LITE_GPU` must add `mesa-switch-woa/tegra-x1-wddm` to the include path;
GPU driver implementation and test records belong in that main repository.
