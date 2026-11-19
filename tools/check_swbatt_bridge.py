#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""OLED (Aula): swbatt.sys provides the fan / GPU power bridge (\\Device\\SwitchBacklight) instead of SwitchFan.
Compares swbatt's BRIDGE_* codes with the bridge ABI (windows/pmic.h) of the pinned mesa-switch-woa checkout.

  python3 tools/check_swbatt_bridge.py --woa external/mesa-switch-woa

Exit 1 when a code swbatt implements differs from the pinned ABI (name->value) or the IOCTL differs.
Codes the pinned ABI has but swbatt lacks are only REPORTED (swbatt answers them STATUS_INVALID_PARAMETER, fail-closed);
swbatt.c is byte-checked against upstream (tools/aula_layout.py), so it is not edited here."""
import argparse, re, sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
ap = argparse.ArgumentParser()
ap.add_argument("--woa", type=Path, required=True, help="mesa-switch-woa checkout (tegra-x1-wddm/windows/pmic.h)")
a = ap.parse_args()

pin = (root / "tools/ci/mesa-switch-woa.rev").read_text().strip()
pmic = a.woa / "tegra-x1-wddm/windows/pmic.h"
if not pmic.is_file():
    sys.exit(f"missing {pmic}")
sw = (root / "drivers/t214-aula/drivers/SwitchBattery/src/swbatt.c").read_text(encoding="utf-8", errors="replace")
up = pmic.read_text(encoding="utf-8", errors="replace")

def defs(text, prefix):
    return {m.group(1): int(m.group(2), 16)
            for m in re.finditer(r"#define\s+(" + prefix + r"\w*)\s+(0x[0-9a-fA-F]+)u?", text)}

ours = {k.replace("BRIDGE_", "", 1): v for k, v in defs(sw, "BRIDGE_").items()}      # QUERY_ADDRESS ...
theirs = {k.replace("TG_GPU_PMIC_", "", 1): v for k, v in defs(up, "TG_GPU_PMIC_").items()}
theirs_ioctl = defs(up, "TG_GPU_IOCTL_PMIC").get("TG_GPU_IOCTL_PMIC")
bad = []
if ours.get("IOCTL") != theirs_ioctl:
    bad.append(f"IOCTL: swbatt {ours.get('IOCTL')} != ABI {theirs_ioctl}")
ours.pop("IOCTL", None)
for k, v in sorted(ours.items(), key=lambda x: x[1]):
    if k not in theirs:
        bad.append(f"{k}=0x{v:x}: not in the pinned ABI")
    elif theirs[k] != v:
        bad.append(f"{k}: swbatt 0x{ours[k]:x} != ABI 0x{theirs[k]:x}")
missing = sorted((v, k) for k, v in theirs.items() if k not in ours)
print(f"mesa-switch-woa pin {pin}")
print(f"swbatt implements {len(ours)} of {len(theirs)} bridge codes")
for v, k in missing:
    print(f"  not implemented (-> STATUS_INVALID_PARAMETER): 0x{v:x} {k}")
for b in bad:
    print("MISMATCH:", b)
print("result:", "FAIL" if bad else "OK")
sys.exit(1 if bad else 0)
