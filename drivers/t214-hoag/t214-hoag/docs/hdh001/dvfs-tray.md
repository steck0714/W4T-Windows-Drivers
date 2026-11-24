# T210B01 CPU/GPU DVFS and desktop tools

## Noble alignment — 2026-11-15

Fan33/Core7/Battery14 is installed with the retained `.158` KMD, bit14,
UMD/ICD, login+5s activation and bidirectional GPU bridge. After installation
checks, the owner tested the device and reported success ("完美"). This is the
accepted working baseline for the next landscape work. The report did not
specify workload duration or individual frequency coverage; it is not recorded
as exhaustive stress, landscape or Vulkan WSI acceptance.

The user canceled temporary RAM locking; automatic scaling remains enabled.
First-use/reset defaults to Handheld. Power and fan choices are saved separately;
the final verified selection was OC_CPU (2), Cool (2), with the85% charging
limit (80–90% band). SSH/SFTP is disconnected for owner testing; local publication
and landscape development continue without reconnecting to the device.

| Item | Confirmed source or device evidence | Windows status |
|---|---|---|
| EMC trained points | Shared SD BPMP has three base tables204/1331.2/1600 and one ARC slot; Noble selects2500MHz at1175/640mV | Native enumeration:204/1331.2/1600/2500MHz. Selectable boot OC values are not simultaneous intermediate runtime OPPs |
| ACTMON | Actual Noble Lite DT: SPI45,20ms,60%up/40%down,1up/3down windows,K=7,count weight1024,16MHz boost with200%/50% coefficients | Fan33 uses watermark interrupts and average watermark rearming, masks saturated directions and removes the extra300ms polling holdoff. Real handled-IRQ status verified |
| CPU bandwidth client | Lite DT requests EMC floors68/102/204/408/665.6/1600/3200MHz at CPU102/204.8/307.2/403/710.4/921.6/1581MHz | Fan33 aggregates successful CPU-transition votes with ACTMON/display demand, capped and rounded to trained points. No BPMP/PMIC operation under the CPU/IRQ lock |
| GPU bandwidth client | NVGPU uses PMU busy/load counters, GPU clock and its bandwidth ratio | PMU-based GPU EMC client is not yet ported; queue activity is not an equivalent measurement |
| CPU governor | Noble uses schedutil; pinned defaults up500us/down2000us and scheduler utilization/capacity margin | Current250ms busiest-core hint and linear min/max mapping differ. AggressiveSLT voltage tables do not imply governor equivalence |
| GPU governor | Noble uses nvhost_podgov,25ms polling; pinned defaults50ms block,90%busy/70%target,smoothing10,damping7 | Current queue/burst and200ms idle policy differ. HighOPT voltage/OPP support does not imply podgov equivalence |
| Native installation | Boot16:11:46UTC; tgd3d enabled16:14:58UTC; final read16:18:12UTC | Fan33, `.158` Code0, GPU bridge, RAM1600MHz,532 completed transitions, sample age0ms, EMC/enumeration/ACTMON/QoS errors0, IRQ flag256 set |
| Tools and owner test | Both apps responding in interactive session1; owner subsequently reported completed testing and success | Accepted as the working baseline; no agent workload test was run on Fan33 and no new physical landscape/Vulkan result is claimed |

Noble SD image SHA256:`86099dace65f2192e32845d448b5f4b038c58537f92fe80bd1c926049018e1a8`.
BPMP SHA256:`19e907f80a8760da39d6a97fae825955daacccc6c1a382e22df52169e39ac09b`.
These establish boot configuration; later Linux debugfs overrides were not read.
Source references: [T210 ACTMON DT](https://gitlab.com/switchroot/kernel/l4t-soc-t210/-/blob/0d7816046cb06b637a3b70381a5e4994fd897c35/kernel-dts/tegra210-soc/tegra210-soc-actmon.dtsi),
[Mariko four-table boot setup](https://github.com/CTCaer/hekate/blob/8e1392df4569ac501a115867056b13c361429e9e/bootloader/l4t/l4t.c#L734-L807),
[CPU floor](https://github.com/CTCaer/switch-l4t-kernel-nvidia/blob/76e6d48970b451c242c20f298b8d63027836bb0b/drivers/platform/tegra/cpu_emc.c),
[CPU callback](https://github.com/CTCaer/switch-l4t-kernel-nvidia/blob/76e6d48970b451c242c20f298b8d63027836bb0b/drivers/cpufreq/tegra210-cpufreq.c),
[NVGPU memory vote](https://github.com/CTCaer/switch-l4t-kernel-nvgpu/blob/1ae0167d360287ca78f5a2572f0de42594140312/drivers/gpu/nvgpu/os/linux/platform_gk20a_tegra.c).

Host checks cover watermark registers, coalesced events, fault masking,
CPU-floor boundaries, trained-cap aggregation, voltage/QoS ordering and DFLL
recovery. ARM64 build, SYS/CAT signatures and native catalog membership passed.
ACPI adds only FAN0's GSIV77 resource; the other device bodies, including GPU0,
are retained. The54 source identities in the Fan33 build manifest matched the
publication worktree before documentation updates.

| Installed component | SHA256 |
|---|---|
| Fan33 SYS | `639a28de7c9d2da157e8b47e88a8ddd7400651c352d3c4ca3c403bae52af7437` |
| Display/platform ACPI | `0502bd0aed0aa3e236d24b0977d389bf04c64d777ba94cad4c5c27a188b9e438` |
| `.158` KMD | `e95287cbeb1d2c325baacb9685affd1d24783b22ecadc1801ba1dffcfe7cc25e` |
| tgd3d UMD | `4c8a13d0ded7c1f781cd947edeca2e349d8556548d4e44641810d4815aa7386a` |
| NVK ICD | `95b97df4e83a2a90315a8784d83e75771021093dec34f98362b584d1f546bdff` |
| Switch Power | `45b42d7847867098e9a1eccb4246ebb5dd00b21c838e717b24a8e1de8d96d637` |
| Pacecar | `99e59dbfe2dbed45c7d99c2a5b427b99109311bb59eb2a50f5946e98b9194ea2` |

Matched package ZIP SHA256:
`6849da8e4b01dc218fc642e8941803a242e4ffe75eedca8ab009301d310c67bd`.
Exact installation/readiness receipts and the source manifest are retained in
`OUT/emc-runtime/noble-profile-tools-20261115/`. Recovery preserves the exact
preceding Fan32/Core7/Battery14/`.158` pairing, not an older .59/.161 driver.

Pacecar retains50% background opacity, fresh signed battery net power and cached
RAM readback. Switch Power uses the Switchroot Joy-Con icon. Both tools start
at login. Windows online updates were disabled and verified after a reboot.
SC7 remains disabled; FPS/DX12 and graphics compatibility expansion are deferred.

Power and fan profiles are independent. RAM aggregates ACTMON demand, the CPU bandwidth floor and
the display floor using the trained firmware table in all seven power modes; the power-mode label does
not imply a fixed RAM clock. Normal/OC profiles permit idle CPU/GPU clocks;
Perf_All and Perf_OC_All retain their CPU1224/GPU460.8MHz floors.

Fan tables come from the same-machine Noble DT; the continuous-governor math
comes from CTCaer/switch-l4t-kernel-nvidia
`76e6d48970b451c242c20f298b8d63027836bb0b`. Windows uses the maximum of the
TMP451 SoC and board sensors instead of Linux's skin estimator. Existing CPU
cooling floor and sensor/overtemperature protection remain; this is not a claim
of identical acoustic behavior to Noble.

Battery calibration reference: Hekate
`8e1392df4569ac501a115867056b13c361429e9e`, `bdk/power/max17050.c`.
The5mΩ board has2x current gain; the conversion is156.25µA per signed LSB.
Unknown gauge/calibration or stale/failed reads produce unavailable power.
Positive means charging the battery, negative means discharging. On external
power this is **battery net power**, not AC input, total system or CPU/GPU power.
No charger calibration or limit is changed by telemetry.

The background change affects only panel opacity (50%, per the latest user choice); text retains its
normal opacity. Both previous executables, tasks and the previous Pacecar config
are backed up by `Install-DesktopTools.ps1` before replacement.

SC7 remains disabled; FPS and new graphics compatibility are deferred. The
current Gemini delegation was canceled at the user's instruction before source
changes; this round is implemented and reviewed by the main agent.

## Historical implementation and test records

This candidate completes the CPU clock path missing from the original Gemini
GPU-only delivery (`13eb977`, `9722c5d`, `52aed8d`). SwitchFan 1.0.0.15 owns
CPU/M4 and the shared SPB connection; TegraGpuRender owns GPU/NAPLL transitions.
The current normal boot, deployment, test and recovery baseline is **Handheld**
(mode 1), as selected by the user on 2026-11-14. The retained installed baseline
is now Fan21/Core7/Battery13 and the matched current display stack, confirmed at
07:20 UTC on 2026-11-15. It must not be rolled back to Fan19 while completing EMC.
The original side-delivery notes below are historical.

The user requested a single manual desktop launcher, `打开硬件工具.cmd`, for
reopening either tool after closing it. Its source is
[`Open-HardwareTools.cmd`](../../app/SwitchPowerTray/Open-HardwareTools.cmd).
It opens the installed per-user Pacecar and Switch Power executables without
the boot-only `--startup-handheld` argument or any reset/install operation;
each application retains its existing single-instance behavior. Missing files
or a launch failure leave a readable error instead of closing silently.
This source will be committed/pushed with the completed EMC changes, not in a
separate push. Existing automatic startup configuration is retained.

The Fan23 source correction follows `init_core_dvfs_table()` in Switchroot
`tegra210-dvfs.c` at `2d0059fd3167a8df756de2aa0489d4aa70a9fc15`: when the B01
EMC DVB table exists, it takes precedence over the timing/BPMP voltage table.
The validated Mariko handoff selects the existing process-specific B01/SLT
core and GPU characterization; MRQ32's legacy1100mV points are no longer queried
or used as a voltage requirement. This does not raise the1050mV core ceiling or
add clock rates: supported frequencies still come only from BPMP ROUND_RATE,
and each transition retains rail votes, GPU acknowledgement, display QoS and
GET_RATE confirmation. The resolved raw MRQ32 diagnostic is removed on start.
Host ASan/UBSan controls pass with the native three-point response fixture;
the old Fan22 header fails that control. ARM64 build and native signature
checks pass. Fan23 activated at08:11:31 UTC on boot08:08:22; native enumeration
now returns204000/1331200/1600000kHz with EnumerationError0 and valid ACTMON.
Display QoS validation returns-2 before a successful transition; RAM stays1600,
TransitionCount0, so dynamic hardware acceptance is still pending. See OUT
`emc-runtime/mariko-dvb/DECISION.json` and `phase-5.json`.

Current acceptance status (2026-11-15 UTC):

At 07:56:40 UTC the Fan22 candidate activated on the unchanged current display
stack after one planned restart (boot 07:53:25). Dynamic EMC still fails before
ROUND_RATE: MRQ32 returns 204000/1331200/1600000kHz, all at 1100mV, which exceeds
the controller's validated 1050mV ceiling. Allowing frequency plateaus alone
does not solve this firmware/voltage-contract mismatch. Do not raise the rail
ceiling merely to pass enumeration. Fan21 remains the pinned restore pairing;
the current installed candidate is recorded separately in the Mesa baseline.

| Component | Installed/read back | Remaining acceptance |
|---|---|---|
| Switch Power | Automatically running in session1 after the 07:06:47 reboot; 10-second logon task enabled | Charging-current behavior requires appropriate power/battery conditions |
| Pacecar | Automatically running in session1 after the same reboot; 60-second logon task enabled; native snapshot RAM1600000kHz, valid127, error0 | Dynamic RAM after enumeration repair; FPS explicitly deferred |
| Power policy | Console→Handheld native assertions pass; retained Handheld1, CPU DFLL active, GPU policy1, 85% charging band | Dynamic EMC transitions |
| EMC | Fan23 installed; real204/1331.2/1600MHz enumeration and ACTMON pass; RAM live1600MHz | QoSError-2, TransitionCount0; dynamic adjustment not accepted, inspect PTSA validation next |

The app update did not request a reboot or install drivers. Its previous EXEs and
task XML remain in `LocalAppData\SwitchWOA\Backups\desktop-tools-20261115-052054-856`;
temporary update tasks were removed and no desktop shortcuts were created.
The latest complete component hashes and receipts are recorded in the Mesa
handoff and OUT `emc-runtime/fan21/installed-working-baseline.json`. The native
Console/Handheld assertions passed; their compact JSON summary has null fields
because Select-Object was applied to a Hashtable, a formatting failure recorded
in the receipt. A later full readback confirms Handheld and the expected caps.
The following failed-install chronology is historical, not the retained state.
The first matched update at 06:06 failed before installing platform drivers:
the ordinary firmware DSDT had been packaged as the display override and omitted
GPU0. Restoration completed at 06:12. The corrected display override preserves
all 24 other devices and replaces only FAN0 resources. A subsequent IRAM mailbox
resource fix and unique Fan21 package identity enabled the successful 07:10
native activation. The installed ACPI is 4106 bytes, SHA256 `caeeb93b177f216af19ae6c6418fb2c2bfd25fdaf6e911efb0d77859f0b45315`.
Failed packages must not be retried.

The installed power-button package is now **1.0.0.7**, based on the canonical
polling source, with matching INF/HID/runtime identity. It does not revive the
retired version6 IRQ/idle experiment or enable SC7. Future deployment must back
up this actual installed package and preserve the current full baseline.

## Profiles

IDs 0..6 and their requested caps match the captured Switchroot T210B01
`nvpmodel_t210b01.conf`. ID 7 remains only an internal failure-recovery ABI and is
removed from the tray menu; normal Reset to Defaults selects Handheld and the
85% charging band (80–90%). These are
caps and floors, not promises that the live clock always equals the cap.

| ID | Profile | CPU min / effective max MHz | GPU min / effective max MHz |
|---|---|---:|---:|
| 0 | Console | 102 / 1963.5 | 76.8 / 844.8 |
| 1 | Handheld | 102 / 1581 | 76.8 / 537.6 |
| 2 | OC_CPU | 102 / 2397 | 76.8 / 844.8 |
| 3 | OC_GPU | 102 / 1963.5 | 76.8 / 1228.8 |
| 4 | OC_All | 102 / 2397 | 76.8 / 1228.8 |
| 5 | Perf_All | 1224 / 1963.5 | 460.8 / 844.8 |
| 6 | Perf_OC_All | 1224 / 2397 | 460.8 / 1228.8 |

Switchroot requests 3000000 kHz CPU / 2000000 kHz GPU in its OC profiles. The
actual SLT/HighOPT tables stop at 2397000/1228800 kHz. The captured Noble GPU
list ended at 844800 because its DT capped frequency at 844800 and voltage at
800 mV; it was not the full HighOPT OC table. The status API reports requested
and effective policy caps separately. Thermal/characterization limits can
reduce the actual rate further. RAM is governed separately by ACTMON across the actual BPMP-trained table; the current EMC acceptance is recorded above.

## CPU ownership and voltage

`dfll_tables.h` ports AggressiveSLT CVB/LUT math with original GPL-2.0-only and
NVIDIA/CTCaer notices. It uses this chip's fused CPU Speedo/process, not the
reference chip's voltage as a universal constant. The reference Speedo1630,
process0 case matches all 23 captured OPP voltages and all 33 PMIC LUT entries.
SLT voltages are used only with the closed-loop DFLL, never with open-loop PLLX.
The CPU regulator characterization ceiling is 1235 mV; thermal caps/floors
and the high-tune voltage/settle requirement are enforced.

New WinX ACPI resources expose DFLL control, clocks and reset registers. A
separate versioned record at 0xA9FB0100 carries HIDREV/SKU/Speedo/process/PMIC
address; the old performance ABI remains intact. Missing/invalid resources or
fuses make dynamic profiles unavailable. Firmware does not enable DFLL at boot.

All CPU transitions and frequency samples take CpuLock. The DFLL core verifies
actual CPU mux ownership, PMIC delivery and a fresh frequency counter. Returning
to PLLX prepares and verifies the boot PLL while CPU remains on DFLL, holds a
suitable rail, confirms the mux change, then disables/drains DFLL I2C with the
required two clear PENDING samples. A failed transition latches the failure;
periodic polling cannot silently retry it. Explicit baseline selection is needed
to clear a successfully recovered failure. Failed recovery blocks resource release.

The tray sends the busiest core's utilization every 250 ms using the documented
NtQuerySystemInformation processor-performance layout. This is a Windows load
hint controller, not Linux's cpufreq governor or Windows PPM integration. Kernel
code owns frequency, voltage and thermal bounds. Missing hints retain the chosen
profile ceiling; exiting the tray does not turn off thermal protection. The original login action selected Handheld. The current update restores the
last successfully confirmed user mode once the live GPU provider and CPU DFLL
are available. Its existing status
timer waits for readiness; it sends at most one request and cancels that pending
default if the user chooses a mode first. A failed transition is reported without
periodically reissuing clock writes.

## GPU contract and telemetry

The complete 16-frequency HighOPT table uses the fused GPU Speedo and thermal
range. Normal profiles retain an 800 mV ceiling; OC entries use the upstream
HighOPT ceiling of 900 mV. An OPP exceeding that ceiling in any thermal range
is unavailable. GPU transitions happen only after the worker proves quiescence:
voltage-up before frequency-up; verified frequency-down before voltage-down.
A PMIC write followed by an error/readback failure is reconciled against the
actual rail; stale cached voltage may not authorize another boost.

Kernel-only bridge opcode 0x108 reports live-counter ABI 0x00020001. Legacy KMD
heartbeats cannot mark old clock data as live. The public power status is version
2 (33 DWORDs); setters require `{Version=2, Mode}`, so old Mode0 four-byte setters
are rejected. Counter timestamps and TMP451 sample age distinguish unavailable
or stale readings. GPU counter sampling is at an idle boundary, at most once per
second; a busy/unavailable sample is not fabricated. TMP451 remote/local are
SoC/board temperatures, not independent CPU and GPU junction sensors.

## Tray and charging

The original charging bands remain 55% -> 50–60%, 85% -> 80–90%, Off -> full.
Unlike Switchroot's HIZ drain behavior, this driver keeps external VBUS connected
and changes charging enable only; OTG/PD ownership is retained. This intentional
difference is not a claim of byte-for-byte Linux charging behavior.

Explorer's TaskbarCreated notification recreates icons. Cursor hide/restore keeps
its crash breadcrumb until recovery succeeds. Restore commands return errors
instead of claiming success when a driver or cursor reset failed. The online
restore script resets policies/settings, not driver binaries or firmware.

`app/Shared/Install-StartupTask.ps1` registers per-user interactive logon tasks,
with battery restrictions disabled. Switch Power starts after 10 seconds; Pacecar
after 60 seconds to follow the login display-driver activation. The installer
removes their previous Run entries and records each task under
`HKCU\Software\SwitchWOA\StartupTasks`. Both applications' startup checkboxes
read and change the task's actual Enabled property. Standalone installs without a
task mapping retain the Run-key backend. This uses Microsoft's documented
[interactive task context](https://learn.microsoft.com/en-us/windows/win32/taskschd/security-contexts-for-running-tasks)
and [Enabled property](https://learn.microsoft.com/en-us/windows/win32/taskschd/registeredtask-enabled)
(checked 2026-11-14); no password or SYSTEM desktop process is used.

The obsolete `app/SwitchTelemetry` source and desktop executable are removed at
the user's request. Pacecar's private upstream source is not republished here.
Hardware Details is available only through its explicit menu item. The former
tray double-click binding could open the large details dialog during touch or
rapid clicks; it has been removed. Successful mode/charge changes update status
silently, while actual IOCTL/readback failures still display an error.

2026-11-14 native startup verification: the planned reboot at 23:46:28 UTC started
both applications in session 1 through their login tasks. The shared application
backend successfully disabled/re-enabled both tasks before reboot. Handheld was
read back with CPU DFLL active, GPU policy 1 and no CPU transition error. GPU
remained Code0 with the retained tgd3d UMD/ICD. The two desktop shortcuts were
removed at 23:50:06 UTC, after that verification. This is startup/policy acceptance,
not EMC DVFS or sustained GPU stability acceptance.

Latest user feedback after that observation: Pacecar did not restore after a
later reboot. Treat automatic startup as **not accepted** until the next matched
deployment's boot test resolves that report. Keep the English profile names
without descriptions, and publish the completed changes/status tables in one
push per repository after that test. On 2026-11-15 at 00:12:46 UTC the 85% charge
band was read back with SOC99, CHG_CONFIG disabled, HIZ0, external input online
and LastApplyStatus0; the user selected this band as the normal future baseline.

## Validation and handoff limits

Passed: ASan/UBSan CPU/DFLL table and transition models, real-thread CPU policy
serialization control, GPU voltage/frequency ordering and partial-PMIC failures,
all 16 GPU OPP transitions, battery/USB-PD controls, screen-off fan/CPU ordering;
ARM64 drivers/KMD/tray builds; x64 tray API controls under isolated Wine.
The original unpatched GPU clock code fails the new partial-write regression.

These are software/model checks. No CPU DFLL, OC voltage, GPU rendering, sleep,
charging-boundary, or long-duration stability result is claimed for hardware.
Wine's real system-cursor self-test returned 11; native Windows cursor acceptance
is still pending. Main-thread review must integrate onto its current display KMD,
sign matching packages, preserve its UMD/ICD/display settings, and test baseline,
Handheld/Console, transitions/thermal recovery, then OC independently. Do not use
a generic build-check INF to replace the main thread's native tgd3d package.

References: Switchroot kernel `2d0059fd3167a8df756de2aa0489d4aa70a9fc15`, nvgpu
`1ae0167d360287ca78f5a2572f0de42594140312`; Noble capture SHA256
`87f3b515db3aed79623142f25bda1eedfd5c168288d7bf944b928fc27c415224`.
Microsoft contracts: [WDF timer serialization](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdftimer/ns-wdftimer-_wdf_timer_config),
[processor times](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntquerysysteminformation),
[TaskbarCreated](https://learn.microsoft.com/en-us/windows/win32/shell/taskbar#taskbar-creation-notification).


## Main-review integration corrections (2026-11-14)

Build the GPU-enabled SwitchFan with `MESA_TEGRA` set to the current
mesa-switch-woa/tegra-x1-wddm source directory. The GPU PMIC transport and its
private ABI remain maintained there; the local t214/gpu_pmic.h forwards to it
and implements the Windows CPU/power policy.

The main review adds CpuLock around the cooling snapshot's DFLL readback,
cleans up power notifications even after unsuccessful PLLX restoration, aligns
the diagnostic DriverVersion with INF15, and expires stale power-tray GPU
readings. Returning a failure from ReleaseHardware is not a veto of teardown;
cleanup must not leave callbacks targeting a freed WDF context. CPU source/
voltage is not forcibly changed from that cleanup path.

Local ASan/UBSan DFLL/clock/PMIC/charge/USB controls, screen-off controls and
tray API controls passed; these are not real hardware DVFS or stability results.
The main chat will build/sign the combined package with the current display
source, start native validation in mode7, and preserve exact pre-install files
for recovery. SC7 is outside this delivery.


## First native transition: refused before DFLL activation

The first integrated Fan15 trial successfully reported the version2 power ABI,
Speedo1630/process0, available DFLL resources and fixed CPU1497.6/GPU460.8/RAM1600.
Mode1 was refused with Win32 ERROR_NOT_READY (21); the automatic test stopped at
that first request and verified fixed7 again. No successful dynamic or OC mode
transition is claimed for Fan15.

The adapter had two incorrect clock assumptions: T210 PLLP_OUT0 is the VCO and
ignores BASE.DIVP (OUT2 uses that divider), and WinX's I2C5 parent selector6 is
CLK_M, while the adapter accepted only PLLP. Fan16 derives PLLP_OUT0 correctly
and supports those two actual mux parents without changing shared I2C5 settings.
The version2 private firmware handoff adds ClockMKhz, derived from OSC_CTRL and
SPARE_REG0, so the kernel does not guess a 19.2 MHz rate or read outside its ACPI
resources. Public power IOCTL ABI remains2; fixed policy7 and display code are
unchanged. Unknown mux parents/invalid handoff still fail before ownership.

A DIVP=1 negative control fails on the old adapter. Patched ASan/UBSan controls
cover both Windows CLK_M19.2 and Linux PLLP/5=81.6 MHz parents, the resulting DFLL
I2C divisors and unchanged shared mux. Native transition acceptance is pending.
Reference: Switchroot kernel2d0059fd3167a8df756de2aa0489d4aa70a9fc15
clk-tegra210.c, pll_p_params (TEGRA_PLL_VCO_OUT); WinX ClockLibPrivate.h and
Tegra210 clock.c document the mux and CLK_M divider.


## Fan17: fixed PLLP semantics

Fan16 native mode1 still returned Win32 error21 before its clock-source trace;
fixed7 recovery succeeded and the native display stayed Code0. The pinned
Switchroot clk-pll.c (2d0059fd3167a8df756de2aa0489d4aa70a9fc15,
clk_pll_recalc_rate) treats PLLP with BASE.OVERRIDE clear as fixed408MHz;
its software M/N fields do not determine the rate. Fan17 follows this rule,
checks enable/lock/bypass, and retains VCO rate decoding for software override.
The existing raw clock traces are recorded before rejecting those fields.
No shared PLLP/I2C source is reprogrammed. Firmware handoff remains version2.

The Fan16 adapter fails the fixed-PLLP fixture with unused M/N=0; the corrected
adapter passes under ASan/UBSan along with the software-override/DIVP and
CLK_M/PLLP I2C cases and existing PMIC recovery controls. These are models;
native switching remains pending.


## Fan19: claim only the disabled, reset DFLL handoff

Fan18 located mode1 refusal at the ownership guard (stage2). Native values:
CTRL=0, OUTPUT_CFG=0x50200000, DVCO_RESET=1, RESET_W=0x110037ff,
CLK_ENB_W=0x482880fc, while CPU remained PLLX1497600kHz/1120mV.
The I2C master-enable bit alone was incorrectly treated as an active owner.
Fan19 accepts that disabled/reset case only when forced output is also off.
It inhibits new I2C messages and requires two clear pending reads separated
by at least the upstream 2us minimum before resetting the idle controller.
An active loop, forced voltage output, failed disable or undrained request
still prevents takeover. CPU mux and voltage are unchanged on rejection.

Reference: pinned Switchroot clk-dfll.c dfll_i2c_set_output_enabled and
is_output_i2c_req_pending. The captured-idle fixture fails with Fan18 and
passes with Fan19 under ASan/UBSan; active-owner/pending-request rejection
and exact saved gate/reset restoration are checked. Native acceptance pending.


Native acceptance update: on the main display pairing, Fan19 completed modes
1,0,2,3,4,5,6 and each return to fixed7 at 2026-11-14 22:22:15–22:23:05 UTC.
CPU DFLL active, GPU dynamic flag/fresh counter, temperatures and RAM1600 were
checked. All fixed returns read CPU1497600kHz/1120mV and GPU460800kHz/800000uV;
DWM836 and native Code0 remained. This accepts short mode transitions only,
not every OPP or sustained OC/physical-display stability. The user cancelled
five-minute interaction. Source/build receipts are in the main chat's
mesa-switch-woa-tgd3d-vulkan-out/dvfs-pacecar-integration/dfll-idle-handoff.

## Historical EMC integration notes — 2026-11-15 01:55 UTC

The current recovery/deployment baseline is Handheld with the 85% charging
band (80–90%). Fixed7 remains an internal recovery ABI only. A later user
report invalidated the earlier Pacecar startup acceptance. Its startup path
now reads the existing login task instead of applying a default false config;
the actual settings callback still controls enabling/disabling startup.
The new binary builds for ARM64; a matched native login test remains pending.

Fan20 adds firmware-reserved BPMP channel7 readback, real EMC-rate telemetry,
and coordinated CPU/Core/GPU voltage votes. Core5 uses the existing MAX77620
owner; Battery13 supplies the 85% default when no setting is stored. The GPU
voltage table and GPU-side vote/acknowledgement remain owned by mesa-switch-woa.
Control tests cover voltage ordering, failed transitions, rail retention after
GPU isolation, and missing BPMP tables. These are models, not GPU/DRAM tests.
MC_ALL ACTMON now runs on its own passive 100ms timer. The governor selects
only firmware-enumerated trained rates. Display QoS reserves the fixed physical
78MHz/32bpp window-A demand before changing clocks, preserves LA registers and
other client reservations, and checks firmware-owned PTSA fields. SET_RATE is
wired only after native GPU ownership/voltage acknowledgement is ready; failed
rate/QoS readback retains rail votes and latches the failure. The conservative
PTSA reservation and pinned Linux 2000ns latency assumption require independent
review and native validation. Control success is not DRAM/display acceptance.

Fan20, Core5, Battery13, the matching .158 KMD, firmware and Pacecar build;
all four native Inf2Cat runs and 22 Windows signature/member checks passed.
No new package from this work has been installed. The 01:55 UTC device query
still found .158/Code0 and Switch Power running, but no Pacecar process.
Each future code addition requires one independent Gemini Carbon High audit
through Antigravity CLI; review new differences after an audited snapshot changes.

## Explicit Windows online-update opt-out (2026-11-15)

At the owner's request, `app/Shared/Disable-SystemOnlineUpdates.ps1` was run
separately from all driver/tool installers. Native14:38UTC readback confirms
wuauserv, UsoSvc and WaaSMedicSvc stopped/disabled, all tasks in their update
folders disabled, automatic updates/online driver search disabled and outbound
service/program blocks active under all enabled firewall profiles. The UUS
worker path is covered. WaaSMedicSvc required its own Start=4 registry setting
because its SCM setter rejected access; service ACLs and binaries were not changed.
Post-reboot persistence is still being checked. No original-settings backup or
restore script was created for this opt-out. Driver/app recovery backups are
separate and retained.

Switch Power now embeds the original Switchroot blue/red Joy-Con icon;
[artwork provenance](../../app/SwitchPowerTray/assets/README.md). Menu state and
tooltips retain mode/charge information. Native replacement completed14:29UTC
without reboot; the icon change passed ARM64/x64 build and existing tray controls.
