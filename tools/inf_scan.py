#!/usr/bin/env python3
"""Scan INF files: HWIDs, DriverVer, service, start type, class. Output JSON on stdout.
usage: inf_scan.py <root> [<root>...]"""
import sys, re, json, pathlib

def read(p):
    b = p.read_bytes()
    for enc in ("utf-16", "utf-8-sig", "cp932", "latin-1"):
        try:
            t = b.decode(enc)
            if "[Version]" in t or "[version]" in t.lower():
                return t
        except Exception:
            pass
    return b.decode("latin-1")

def scan(p):
    t = read(p)
    d = {"file": str(p)}
    m = re.search(r"DriverVer\s*=\s*([^\r\n;]+)", t, re.I); d["driverVer"] = m.group(1).strip() if m else None
    m = re.search(r"^\s*Class\s*=\s*(\S+)", t, re.I | re.M); d["class"] = m.group(1) if m else None
    d["hwids"] = sorted(set(x.upper() for x in re.findall(r"((?:ACPI|ROOT)\\[A-Za-z0-9_&]+)", t, re.I)))
    m = re.search(r"StartType\s*=\s*(\d+)", t, re.I); d["startType"] = int(m.group(1)) if m else None
    m = re.search(r"ServiceBinary\s*=\s*%12%\\([^\s;]+)", t, re.I); d["sys"] = m.group(1) if m else None
    m = re.search(r"AddService\s*=\s*([A-Za-z0-9_]+)", t, re.I); d["service"] = m.group(1) if m else None
    d["kmdf"] = (re.search(r"KmdfLibraryVersion\s*=\s*([\d.]+)", t, re.I) or [None, None])[1]
    return d

out = []
for r in sys.argv[1:]:
    for p in sorted(pathlib.Path(r).rglob("*.inf")):
        if "/efifs/" in str(p) or "/edk2/" in str(p):
            continue
        try: out.append(scan(p))
        except Exception as e: out.append({"file": str(p), "error": str(e)})
json.dump(out, sys.stdout, ensure_ascii=False, indent=1)
