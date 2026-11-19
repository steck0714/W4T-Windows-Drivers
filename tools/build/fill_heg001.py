#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Fill build/HEG-001/ from YOUR copy of the windows-switch v0.8.0 drivers zip (Switch OLED, upstream binaries).

Every file is checked against the size and SHA-256 recorded in drivers/t214-aula/manifest/windows-switch-0.8.0.json
before it is written; a file that does not match is skipped and reported. Only agent/** is taken (drivers, agent
service, scripts) - not bl33.bin, not bitmaps, and never firmware. Output paths come from the manifest only
(never from names inside the zip), so a hostile zip cannot write outside build/HEG-001/.

  fill_heg001.py --zip windows-switch-arm64-drivers-v0_8_0.zip [--out build/HEG-001]
"""
import argparse, hashlib, json, os, shutil, sys, zipfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MANIFEST = os.path.join(ROOT, "drivers", "t214-aula", "manifest", "windows-switch-0.8.0.json")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--zip", required=True, help="windows-switch-arm64-drivers-v0_8_0.zip")
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "HEG-001"))
    a = ap.parse_args(argv)
    man = json.load(open(MANIFEST, encoding="utf-8"))
    zname = os.path.basename(a.zip)
    if zname not in man["zips"]:
        sys.exit("%s is not a zip listed in the manifest (%s)" % (zname, ", ".join(man["zips"])))
    zhash = hashlib.sha256(open(a.zip, "rb").read()).hexdigest()
    if zhash != man["zips"][zname]:
        print("warning: the zip's own SHA-256 differs from the manifest; each file is still checked individually")
    out = os.path.abspath(a.out)
    ok, bad = [], []
    with zipfile.ZipFile(a.zip) as z:
        names = set(z.namelist())
        for f in man["files"]:
            p = f["path"]
            if f["zip"] != zname or not p.startswith("agent/"):
                continue
            if p.startswith("/") or ".." in p.split("/") or "\\" in p:
                bad.append((p, "unsafe path in manifest")); continue
            if p not in names:
                bad.append((p, "not in the zip")); continue
            info = z.getinfo(p)
            if info.file_size != f["size"]:
                bad.append((p, "size differs")); continue
            data = z.read(p)
            if hashlib.sha256(data).hexdigest() != f["sha256"]:
                bad.append((p, "SHA-256 differs")); continue
            dest = os.path.join(out, *p.split("/"))
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with open(dest, "wb") as fh:
                fh.write(data)
            ok.append(p)
    for lic in ("LICENSE", "COPYING.md", "CREDITS.md"):
        if os.path.isfile(os.path.join(ROOT, lic)):
            shutil.copyfile(os.path.join(ROOT, lic), os.path.join(out, lic))
    for p in ok:
        print("ok   ", p)
    for p, why in bad:
        print("SKIP ", p, "-", why)
    print("%d file(s) verified and written, %d skipped -> %s" % (len(ok), len(bad), out))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
