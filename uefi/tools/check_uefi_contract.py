#!/usr/bin/env python3
"""Keep the EDK II hand-off code and the Windows TegraGpu power backends in agreement.

  python3 -I uefi/tools/check_uefi_contract.py        (from the repo root)

Checks (all static, no hardware, no EDK II):
  1. Every register address / bit in uefi/edk2/GpuHandoff/GpuHandoff.h equals the one used by
     platform/t210_power.c (Erista) and platform/t214_power.c (Lite / OLED).
  2. The expected HIDREV values match what each backend's tg_gpu_power_up() accepts.
  3. Both backends still refuse a GPU that is not (reset asserted AND clamped), i.e. the contract
     the UEFI side exists to satisfy has not been loosened or changed.
  4. uefi/edk2/GpuHandoff/GpuHandoff.{h,c} are byte-identical to the files carried in
     uefi/patches/winx-gpu-handoff.patch (so the patch and the dev copy cannot drift apart).
"""
import pathlib, re, sys

root = pathlib.Path(__file__).resolve().parents[2]
t210 = root / "drivers/t210-icosa/drivers/TegraGpu/platform/t210_power.c"
t214 = root / "drivers/t214-hoag/TegraGpu/platform/t214_power.c"
hdr = root / "uefi/edk2/GpuHandoff/GpuHandoff.h"
csrc = root / "uefi/edk2/GpuHandoff/GpuHandoff.c"
patch = root / "uefi/patches/winx-gpu-handoff.patch"
errors = []


def defines(path):
    out = {}
    for line in path.read_text().splitlines():
        m = re.match(r"\s*#define\s+(\w+)\s+(.+?)\s*(?:/\*.*)?$", line)
        if m and "(" not in m.group(1):
            out[m.group(1)] = m.group(2).strip()
    return out


def value(name, table, depth=0):
    if depth > 8 or name not in table:
        raise KeyError(name)
    expr = re.sub(r"\b(0x[0-9A-Fa-f]+|\d+)[uU]\b", r"\1", table[name])
    for ident in set(re.findall(r"\b[A-Za-z_]\w*", expr)):
        if ident in table:
            expr = re.sub(r"\b%s\b" % ident, "(%d)" % value(ident, table, depth + 1), expr)
    if re.search(r"\b[A-Za-z_]", expr):
        raise ValueError(f"{name}: unresolved identifier in {expr!r}")
    return eval(expr, {"__builtins__": {}})


H = defines(hdr)
pairs = [("GH_CAR_CLOCK_X", "CAR_CLOCK_X"), ("GH_CAR_CLOCK_SET", "CAR_CLOCK_SET"),
         ("GH_CAR_CLOCK_CLR", "CAR_CLOCK_CLR"), ("GH_CAR_RESET_X", "CAR_RESET_X"),
         ("GH_CAR_RESET_SET", "CAR_RESET_SET"), ("GH_CAR_RESET_CLR", "CAR_RESET_CLR"),
         ("GH_GPU_GATE", "GPU_GATE"), ("GH_GPU_REF", "GPU_REF"), ("GH_GPU_CLOCKS", "GPU_CLOCKS"),
         ("GH_PMC_GPU_CLAMP", "PMC_GPU_CLAMP"), ("GH_MC_FLUSH", "MC_FLUSH"),
         ("GH_MC_FLUSH_STATUS", "MC_FLUSH_STATUS"), ("GH_MC_GPU_BIT", "MC_GPU_BIT")]
for backend in (t210, t214):
    B = defines(backend)
    for gh, drv in pairs:
        try:
            a, b = value(gh, H), value(drv, B)
        except (KeyError, ValueError) as e:
            errors.append(f"{backend.name}: cannot evaluate {gh}/{drv}: {e}")
            continue
        if a != b:
            errors.append(f"{backend.name}: {drv}={b:#x} but {gh}={a:#x}")
    text = backend.read_text()
    if not re.search(r"!\(io->read32\(io->context, CAR_RESET_X\) & GPU_GATE\)\s*\|\|\s*"
                     r"!\(io->read32\(io->context, PMC_GPU_CLAMP\) & 1u\)", text):
        errors.append(f"{backend.name}: BUSY precondition (reset asserted and clamped) changed")
    if "return TG_GPU_POWER_BUSY" not in text:
        errors.append(f"{backend.name}: no TG_GPU_POWER_BUSY return found")

# HIDREV
B210 = defines(t210)
try:
    if value("HIDREV_T210_MASK", B210) != value("GH_HIDREV_MASK", H):
        errors.append("HIDREV mask differs between t210_power.c and GpuHandoff.h")
    if value("HIDREV_T210_VALUE", B210) != value("GH_HIDREV_T210", H):
        errors.append("T210 HIDREV differs between t210_power.c and GpuHandoff.h")
except (KeyError, ValueError) as e:
    errors.append(f"t210 HIDREV: {e}")
m = re.search(r"&\s*(0x[0-9a-fA-F]+)u?\)\s*!=\s*(0x[0-9a-fA-F]+)u?\)\s*\n?\s*return TG_GPU_POWER_UNSUPPORTED_BOARD",
              t214.read_text())
if not m:
    errors.append("t214_power.c: HIDREV check not found")
else:
    if int(m.group(1), 16) != value("GH_HIDREV_MASK", H) or int(m.group(2), 16) != value("GH_HIDREV_T210B01", H):
        errors.append(f"t214 HIDREV {m.group(1)}/{m.group(2)} differs from GpuHandoff.h")

# patch vs dev copy
ptxt = patch.read_text()
for rel, dev in (("Library/PlatformBootManagerLib/GpuHandoff.h", hdr),
                 ("Library/PlatformBootManagerLib/GpuHandoff.c", csrc)):
    sec = re.search(r"^diff --git a/%s b/%s\n.*?^@@[^\n]*\n(.*?)(?=^diff --git |\Z)" % (re.escape(rel), re.escape(rel)),
                    ptxt, re.S | re.M)
    if not sec:
        errors.append(f"patch does not add {rel}")
        continue
    body = "".join(l[1:] + "\n" for l in sec.group(1).split("\n") if l.startswith("+"))
    if body != dev.read_text():
        errors.append(f"{dev.relative_to(root)} differs from the copy in uefi/patches/winx-gpu-handoff.patch")

if errors:
    print("MISMATCH", *errors, sep="\n  ", file=sys.stderr)
    sys.exit(1)
print("uefi hand-off constants match t210_power.c / t214_power.c; dev copy matches the patch")
