#!/usr/bin/env python3
"""W4T HWID compatibility check.

Cross-checks, per model, that every INF hardware ID is published by the WinX DSDT
(acpi/Dsdt.*.asl), and lists DSDT devices that no INF binds to.
Static only: it does not prove a driver runs, only that the IDs line up.

usage: check_hwid_matrix.py            (run from the repo root)
exit 0 = no unresolved INF ids, 1 = at least one INF id absent from the DSDT
"""
import re, sys, json, pathlib, subprocess

ROOT = pathlib.Path(__file__).resolve().parent.parent
DSDT = next((ROOT / "acpi").glob("Dsdt.*.asl"))

# Windows-inbox / class IDs published by the DSDT that need no custom INF.
INBOX = {"PNP0D10", "PNP0D20", "PNP0D40", "NVD0D40", "ACPI0007", "ACPI0011"}
# IDs that only exist at runtime or for a given build flag (not a mismatch).
RUNTIME_OK = {
    "SWOA0D03": "Erista Wi-Fi: published only with -D SWITCH_WIFI_ERISTA",
    "SWOA0D01": "Wi-Fi/Wlan: published only with -D SWITCH_WIFI_RUNTIME (header macro SWIFI_ACPI_HID)",
    "SWOA0F01": "GPU: published only with -D SWITCH_LITE_GPU (macro T214_GPU_ACPI_ID)",
    "SWOA0F02": "Erista GPU: published only with -D SWITCH_ERISTA_GPU (acpi/t210)",
    "SWOA0902": "Lite SIO: Lite-only build flag",
}

def dsdt_ids(text):
    ids = set(re.findall(r'"((?:SWOA|NVDA|NVD|WINX|PNP|ACPI)[0-9A-Z]+)"', text))
    macros = {}
    for h in (ROOT / "drivers").rglob("*.h"):
        for m in re.finditer(r'#define\s+(SWIFI_ACPI_HID|T214_GPU_ACPI_ID)\s+"([^"]+)"', h.read_text(errors="ignore")):
            macros[m.group(1)] = m.group(2)
    for name, val in macros.items():
        if name in text: ids.add(val)
    return ids

def scan_infs():
    out = subprocess.check_output([sys.executable, "-I", str(ROOT / "tools/inf_scan.py"), str(ROOT / "drivers")])
    return json.loads(out)

def main():
    ids = dsdt_ids(DSDT.read_text(errors="ignore"))
    infs = scan_infs()
    bad = 0; bound = set()
    print(f"DSDT ids found: {len(ids)}\n")
    for model in ("t210-icosa", "t214-hoag"):
        print(f"== {model}")
        for d in infs:
            f = d["file"]
            if f"/{model}/drivers/" not in f: continue
            name = f.split(f"/{model}/drivers/")[1]
            for h in d["hwids"]:
                hid = h.split("\\")[-1]
                if hid.startswith("WINXDISP"): continue
                bound.add(hid)
                if hid in ids:        st = "ok"
                elif hid in RUNTIME_OK: st = "flag: " + RUNTIME_OK[hid]
                else:                 st = "MISSING in DSDT"; bad += 1
                print(f"  {name:62s} {hid:10s} {d['driverVer'] or '?':22s} {st}")
        print()
    unbound = sorted(i for i in ids if i not in bound and i not in INBOX)
    print("DSDT ids with no INF in this repo:", ", ".join(unbound) or "-")
    print("\nresult:", "FAIL" if bad else "OK")
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
