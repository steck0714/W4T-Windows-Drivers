#!/usr/bin/env python3
"""Check acpi/t210/gpu0.asl.inc against t210_resources.h (addresses, sizes, GSIs, I2C order)."""
import re, pathlib, sys
root = pathlib.Path(__file__).resolve().parents[1]
h = (root / "drivers/t210-icosa/drivers/TegraGpu/platform/t210_resources.h").read_text()
a = (root / "acpi/t210/gpu0.asl.inc").read_text()
def d(name): return int(re.search(r"#define\s+%s\s+(0x[0-9A-Fa-f]+|\d+)" % name, h).group(1), 0)
want = [(d("T210_GPU_REG_BASE"), d("T210_GPU_REG_SIZE")), (d("T210_GPU_BAR1_BASE"), d("T210_GPU_BAR1_SIZE")),
        (d("T210_GPU_AUX_BASE"), d("T210_GPU_AUX_SIZE"))]
want += [(int(x, 16), int(y, 16)) for x, y in re.findall(r"X\((0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+)\)", h)]
got = [(int(x, 16), int(y, 16)) for x, y in re.findall(r"Memory32Fixed \(Read\w+,\s*(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+)\)", a)]
got = [g for g in got if g[0] != 0x60004414]  # optional LIC window
ok = sorted(want) == sorted(got)
ok &= re.search(r'_HID, "%s"' % re.search(r'ACPI_ID\s+"(\w+)"', h).group(1), a) is not None
ok &= [int(x, 16) for x in re.findall(r"I2cSerialBusV2 \((0x[0-9A-F]+)", a)] == [0x1C, 0x3C]
ok &= re.findall(r"Interrupt .*\{ (\d+) \}", a) == [str(d("T210_GPU_STALL_GSI")), str(d("T210_GPU_NONSTALL_GSI"))]
print("ACPI resources match t210_resources.h" if ok else "MISMATCH", file=sys.stdout if ok else sys.stderr)
sys.exit(0 if ok else 1)
