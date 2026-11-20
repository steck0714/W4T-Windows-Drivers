# uefi/ — UEFI / EDK II side development sources

[日本語](README-ja.md) / English / [简体中文](README-zh-CN.md) / [Español](README-es.md) / [Português (Brasil)](README-pt-BR.md) / [Français](README-fr.md) / [Deutsch](README-de.md) / [한국어](README-ko.md)

Sources and development tools on the UEFI side that make the Windows drivers (`drivers/`) **work reliably when
Windows is started from EDK II (UEFI)**. **No firmware images or binaries (BL33, bl31, ...) are included**; those are
build outputs of winx itself. Not tested on hardware. It has not been built with EDK II (ARM64) either
(see "Verified / not verified" below).

## Why this exists

TegraGpu's `tg_gpu_power_up()` (`platform/t210_power.c` = Erista, `platform/t214_power.c` = Lite / OLED) refuses to
start and returns `TG_GPU_POWER_BUSY` unless the GPU is **in reset and clamped** (CAR `RESET_X` bit 24 and PMC
`0x7000e6d4` bit 0 are both 1). This keeps the driver from taking over a GPU that somebody else is using.
The OLED upstream implementation (`...gpu-gputestonly.patch` in DanilKhmyrov/windows-switch) has the same premise:
"hand the GPU over in reset and clamped; the driver powers it up". So the job of the UEFI side is **not to power the
GPU on, but to hand it to Windows in that state**.

## Layout

| Path | Contents |
|---|---|
| `edk2/GpuHandoff/GpuHandoff.h` | EDK II independent core (register access only). Can be tested on a host as is |
| `edk2/GpuHandoff/GpuHandoff.c` | EDK II side (goes into winx's `PlatformBootManagerLib`). Checks right before Windows starts and again at `ExitBootServices` |
| `edk2/GpuHandoffApp/` | UEFI Shell tool `GpuHandoff.efi` (inspect / fix the state during development without rebuilding BL33). In winx it is carried by the patch as `Drivers/GpuHandoffApp/` |
| `patches/winx-gpu-handoff.patch` | Patch for `usually` in steck0714/winx (placement of `GpuHandoff.c/.h`, DSC flags, 3 call sites, host test, the Shell tool `Drivers/GpuHandoffApp/` and its DSC entries) |
| `tests/gpu_handoff/` | Fake-register-model test (cold / running / MC stuck / other SoC / bus error / bad argument) and a syntax check of the EDK II `.c` files |
| `tests/shim/` | Minimal headers for the syntax check only (not a replacement for EDK II) |
| `tools/check_uefi_contract.py` | Static check that registers, bits, HIDREV and the BUSY condition agree with the driver-side power backends. Also checks that the copies in `edk2/` match the patch |

## Usage

```
tools/run_host_tests.sh                       # everything (includes uefi/)
sh uefi/tests/gpu_handoff/run.sh              # uefi/ tests only
python3 -I uefi/tools/check_uefi_contract.py  # agreement with the drivers only
```

### Applying to winx

```
cd winx && git apply ../W4T-Windows-Drivers/uefi/patches/winx-gpu-handoff.patch
build ... -D SWITCH_ERISTA_GPU=1   # Erista (needs SWITCH_BOARD_ERISTA and Hekate)
build ... -D SWITCH_OLED_GPU=1     # OLED (needs SWITCH_BOARD_OLED and Hekate)
build ... -D SWITCH_LITE_GPU=1     # Lite (existing flag; called from LiteHomeBoot)
```

Call sites: for Erista / OLED, the "Windows" boot option in `SwitchBootMenu.c`; for Lite, `BootWindowsFromMenu()` in
`LiteHomeBoot.c`. All of them call `PlatformGpuHandoffBeforeBoot()`.

### Checking on a real device (UEFI Shell)

```
fs0:\> GpuHandoff.efi        read only. If the GPU is cold, the Windows driver can take it over
fs0:\> GpuHandoff.efi -fix   restore, then print before and after
```
`GpuHandoff.efi` is built only when one of the GPU flags above is set. It is not put into the FDF; like
XusbInit.efi, copy it from the build output to the SD card. A build without the flags is unchanged.

## Behavior guarantees (GpuHandoff.h)

- Never touches power, the PMIC or any PLL.
- If the GPU is already cold, **nothing is written** (calling it repeatedly gives the same result).
- Only when it is not cold, it restores the state in the same order as `tg_gpu_power_down()`
  (MC drain, clamp, reset, clock stop). **If the MC does not drain, clamp and reset are left alone** and the problem
  is only reported (clamping a GPU that is still a bus master is more dangerous).
- Nothing is written for another SoC (HIDREV mismatch) or when a read returns all ones.
- No failure stops the boot.

## Verified / not verified

- Verified: host tests, syntax check of the EDK II `.c` files (Erista / Lite / OLED configurations),
  agreement with the driver side on registers, HIDREV and the BUSY condition, and that the patch applies to winx
  `usually`. The consistency check was confirmed to actually fail when a driver-side value is broken (it also fails
  when the Shell tool copy or the DSC entries drift from the patch).
- Not verified: **building with EDK II (ARM64) (including the DSC entries for `GpuHandoff.efi`) and behavior on
  real hardware**. The OLED call site assumes winx's `SwitchBootMenu.c`; the DanilKhmyrov layout may place it elsewhere.
- Not included: exposing ACPI GPU0 only when ready (the `WifiPrepareBoot.c` approach used for Wi-Fi), and UEFI-side
  control of the GPU rails.
