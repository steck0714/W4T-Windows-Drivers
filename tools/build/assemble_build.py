#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Copy a CI driver build into build/<MODEL>/drivers/ (what tools/ci/Build-AllDrivers.ps1 produced under build/t210/kit).

Only .sys / .inf / .cat / .cer and the build report are copied. Private keys are refused outright.
Writes build/<MODEL>/MANIFEST.json (SHA-256, size, DriverVer and hardware IDs from the INF) and SHA256SUMS.txt,
and copies LICENSE / COPYING.md / CREDITS.md next to the binaries (GPL-2.0 parts ship with their licence and source pointer).

  assemble_build.py --model HAC-001 --kit-dir build/t210/kit [--build-info build/t210/BUILD-INFO.md] [--out build]
"""
import argparse, hashlib, json, os, re, shutil, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
COPY_EXT = {".sys", ".inf", ".cat", ".cer"}
FORBIDDEN_EXT = {".pfx", ".p12", ".pvk", ".key", ".pem", ".snk"}


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def inf_info(path):
    try:
        raw = open(path, "rb").read()
    except OSError:
        return {}
    text = raw.decode("utf-16" if raw[:2] in (b"\xff\xfe", b"\xfe\xff") else "utf-8", "replace")
    ver = re.search(r"(?im)^\s*DriverVer\s*=\s*([^\r\n;]+)", text)
    ids = sorted(set(re.findall(r"(?i)\b(?:ACPI|ROOT|PCI|USB|HID)\\[A-Za-z0-9_&\\-]+", text)))
    return {"driver_ver": ver.group(1).strip() if ver else None, "hardware_ids": ids}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--model", required=True)
    ap.add_argument("--kit-dir", required=True)
    ap.add_argument("--build-info")
    ap.add_argument("--out", default=os.path.join(ROOT, "build"))
    a = ap.parse_args(argv)

    models = json.load(open(os.path.join(ROOT, "build", "models.json")))["models"]
    m = models.get(a.model)
    if not m:
        sys.exit("unknown model %r (have: %s)" % (a.model, ", ".join(models)))
    if m["build"] != "ci":
        sys.exit("model %s is not built by CI here (status: %s). %s" % (a.model, m["status"], m["note"]))
    kit = os.path.abspath(a.kit_dir)
    if not os.path.isdir(kit):
        sys.exit("kit dir not found: " + kit)
    for d, _, fs in os.walk(kit):
        for f in fs:
            if os.path.splitext(f)[1].lower() in FORBIDDEN_EXT:
                sys.exit("refusing: private key material in the kit: " + os.path.join(d, f))

    dest = os.path.join(os.path.abspath(a.out), a.model)
    drv = os.path.join(dest, "drivers")
    shutil.rmtree(drv, ignore_errors=True)  # only the directory this script owns
    os.makedirs(drv)
    files = []
    for d, _, fs in sorted(os.walk(kit)):
        for f in sorted(fs):
            if os.path.splitext(f)[1].lower() not in COPY_EXT:
                continue
            rel = os.path.relpath(os.path.join(d, f), kit).replace(os.sep, "/")
            target = os.path.join(dest, rel) if (rel.endswith(".cer") and "/" not in rel) else os.path.join(drv, rel)
            os.makedirs(os.path.dirname(target), exist_ok=True)
            shutil.copyfile(os.path.join(d, f), target)
            ent = {"path": os.path.relpath(target, dest).replace(os.sep, "/"), "size": os.path.getsize(target), "sha256": sha256(target)}
            if f.lower().endswith(".inf"):
                ent.update(inf_info(target))
            files.append(ent)
    if not any(e["path"].endswith(".sys") for e in files):
        sys.exit("the kit contains no .sys: nothing to assemble")
    if a.build_info and os.path.isfile(a.build_info):
        shutil.copyfile(a.build_info, os.path.join(dest, "BUILD-INFO.md"))
    for lic in ("LICENSE", "COPYING.md", "CREDITS.md"):
        if os.path.isfile(os.path.join(ROOT, lic)):
            shutil.copyfile(os.path.join(ROOT, lic), os.path.join(dest, lic))
    with open(os.path.join(dest, "MANIFEST.json"), "w") as f:
        json.dump({"schema": 1, "model": a.model, "family": m["family"], "files": files}, f, indent=1)
        f.write("\n")
    with open(os.path.join(dest, "SHA256SUMS.txt"), "w") as f:
        for e in files:
            f.write("%s  %s\n" % (e["sha256"], e["path"]))
    print("%s: %d file(s) -> %s" % (a.model, len(files), dest))


if __name__ == "__main__":
    main()
