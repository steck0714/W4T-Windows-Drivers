#!/usr/bin/env python3
"""Add the Erista GPU node to a WinX DSDT (Dsdt.*.asl). Idempotent.

  python3 apply_gpu_patch.py path/to/Dsdt.winx-usually.asl

Changes, all under `#ifdef SWITCH_ERISTA_GPU` so other builds are unchanged:
  1. CPUC CAR window 0x60006000/0x1000 -> 0x60006000/0x100. switchcpu.sys only
     touches 0x020, 0x0E0 and 0x0E4, and the full-page claim overlaps the GPU
     clock/reset registers at 0x60006280 and PLLP_OUT5 at 0x6000667C.
  2. PWRB MAX77620 0x3C connection Exclusive -> Shared, so GPU0 can use the
     same I2C5 target for GPIO6 (EN of the GPU rail).
  3. GPU0 (SWOA0F02) inserted from gpu0.asl.inc.
Build the DSDT with -DSWITCH_ERISTA_GPU=1 (add -DSWITCH_ERISTA_GPU_IRQ=1 only
after the interrupt path is tested).
"""
import pathlib, sys

path = pathlib.Path(sys.argv[1])
text = path.read_text(encoding="utf-8")
if "SWITCH_ERISTA_GPU" in text:
    print("already patched"); sys.exit(0)
here = pathlib.Path(__file__).resolve().parent
gpu = (here / "gpu0.asl.inc").read_text(encoding="utf-8")

cpuc_old = "                Memory32Fixed (ReadWrite, 0x60006000, 0x00001000)\n"
cpuc_new = ("#ifdef SWITCH_ERISTA_GPU\n"
            "                Memory32Fixed (ReadWrite, 0x60006000, 0x00000100)\n"
            "#else\n" + cpuc_old + "#endif\n")
pwrb_old = '                I2cSerialBusV2 (0x003C, ControllerInitiated, 400000, AddressingMode7Bit, "\\\\_SB.I2C5", 0x00, ResourceConsumer, , Exclusive, )\n'
pwrb_new = ("#ifdef SWITCH_ERISTA_GPU\n" + pwrb_old.replace("Exclusive", "Shared") +
            "#else\n" + pwrb_old + "#endif\n")
anchor = "#ifdef SWITCH_LITE_DISPLAY\n        // Display-only ownership"
for name, old in (("CPUC window", cpuc_old), ("PWRB connection", pwrb_old), ("insert point", anchor)):
    if text.count(old) != 1:
        sys.exit(f"cannot patch: {name} found {text.count(old)} times (expected 1)")
text = text.replace(cpuc_old, cpuc_new).replace(pwrb_old, pwrb_new)
text = text.replace(anchor, gpu + anchor)
path.write_text(text, encoding="utf-8")
print("patched", path)
