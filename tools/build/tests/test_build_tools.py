#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Tests for assemble_build.py and fill_heg001.py with synthetic data only (no real driver binaries)."""
import hashlib, json, os, shutil, subprocess, sys, tempfile, zipfile

HERE = os.path.dirname(os.path.abspath(__file__)); TOOLS = os.path.dirname(HERE)
ROOT = os.path.abspath(os.path.join(TOOLS, "..", ".."))
fails = []
def check(name, cond, extra=""):
    print(("PASS " if cond else "FAIL ") + name + ((" :: " + extra) if (extra and not cond) else ""))
    if not cond: fails.append(name)
def run(*a): p = subprocess.run([sys.executable, "-I"] + list(a), capture_output=True, text=True); return p.returncode, p.stdout + p.stderr

tmp = tempfile.mkdtemp(prefix="buildtools-")
try:
    # ---- assemble_build.py ----
    kit = os.path.join(tmp, "kit"); os.makedirs(os.path.join(kit, "Switch-Fan"))
    open(os.path.join(kit, "Switch-Fan", "switchfan.sys"), "wb").write(b"MZ" + b"\0" * 100)
    open(os.path.join(kit, "Switch-Fan", "switchfan.cat"), "wb").write(b"cat")
    open(os.path.join(kit, "Switch-Fan", "switchfan.inf"), "w").write("[Version]\nDriverVer = 11/17/2026,1.0.0.3\n[M]\n%x%=D,ACPI\\SWOA0401\n")
    open(os.path.join(kit, "Switch-Fan", "install.ps1"), "w").write("not copied")
    open(os.path.join(kit, "SwitchWOA-Test.cer"), "wb").write(b"cer")
    open(os.path.join(kit, "BUILD-INFO.md"), "w").write("info")
    out = os.path.join(tmp, "out"); os.makedirs(os.path.join(out, "HAC-001", "drivers"))
    open(os.path.join(out, "HAC-001", "drivers", "stale.sys"), "wb").write(b"old")
    shutil.copyfile(os.path.join(ROOT, "build", "models.json"), os.path.join(out, "models.json"))
    rc, log = run(os.path.join(TOOLS, "assemble_build.py"), "--model", "HAC-001", "--kit-dir", kit, "--out", out)
    d = os.path.join(out, "HAC-001")
    check("assemble: exit 0", rc == 0, log)
    check("assemble: sys/inf/cat copied, ps1 not, stale file removed",
          all(os.path.exists(os.path.join(d, "drivers/Switch-Fan", n)) for n in ("switchfan.sys", "switchfan.cat", "switchfan.inf"))
          and not os.path.exists(os.path.join(d, "drivers/Switch-Fan/install.ps1")) and not os.path.exists(os.path.join(d, "drivers/stale.sys")))
    check("assemble: .cer at the model root", os.path.exists(os.path.join(d, "SwitchWOA-Test.cer")))
    man = json.load(open(os.path.join(d, "MANIFEST.json")))
    inf = [f for f in man["files"] if f["path"].endswith(".inf")][0]
    check("assemble: manifest has DriverVer and HWID", inf["driver_ver"] == "11/17/2026,1.0.0.3" and "ACPI\\SWOA0401" in inf["hardware_ids"], str(inf))
    sums = open(os.path.join(d, "SHA256SUMS.txt")).read()
    check("assemble: SHA256SUMS matches files", all(("%s  %s" % (hashlib.sha256(open(os.path.join(d, f["path"]), "rb").read()).hexdigest(), f["path"])) in sums for f in man["files"]))
    open(os.path.join(kit, "key.pfx"), "wb").write(b"secret")
    rc, log = run(os.path.join(TOOLS, "assemble_build.py"), "--model", "HAC-001", "--kit-dir", kit, "--out", out)
    check("assemble: refuses a kit that holds a private key", rc != 0 and "private key" in log, log)
    os.remove(os.path.join(kit, "key.pfx"))
    rc, log = run(os.path.join(TOOLS, "assemble_build.py"), "--model", "HEG-001", "--kit-dir", kit, "--out", out)
    check("assemble: refuses a model that CI does not build", rc != 0 and "not built by CI" in log, log)
    rc, log = run(os.path.join(TOOLS, "assemble_build.py"), "--model", "HAC-001-01", "--kit-dir", kit, "--out", out)
    check("assemble: Iowa has no driver set", rc != 0 and "no-driver-set" in log, log)

    # ---- fill_heg001.py (synthetic zip whose hashes we put in a temp copy of the manifest) ----
    # the script reads the real manifest, so build a zip whose files match NONE of it first: everything must be skipped
    z = os.path.join(tmp, "windows-switch-arm64-drivers-v0_8_0.zip")
    with zipfile.ZipFile(z, "w") as zf:
        zf.writestr("agent/arm64/swhid.sys", b"not the real driver")
        zf.writestr("../evil.txt", b"x")
    o2 = os.path.join(tmp, "heg"); rc, log = run(os.path.join(TOOLS, "fill_heg001.py"), "--zip", z, "--out", o2)
    check("fill: a file with the wrong hash is skipped, nothing escapes", rc == 1 and "SHA-256 differs" in log and not os.path.exists(os.path.join(o2, "agent/arm64/swhid.sys")) and not os.path.exists(os.path.join(tmp, "evil.txt")), log)
    # now patch a copy of the script's manifest path via env-free approach: write a matching manifest by running the module with a monkeypatch
    code = r'''
import importlib.util, json, hashlib, os, sys, zipfile
spec = importlib.util.spec_from_file_location("f", sys.argv[1]); m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
data = b"synthetic swhid"
man = {"zips": {"windows-switch-arm64-drivers-v0_8_0.zip": "0"*64}, "files": [{"zip": "windows-switch-arm64-drivers-v0_8_0.zip", "path": "agent/arm64/swhid.sys", "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}]}
mp = os.path.join(sys.argv[3], "m.json"); json.dump(man, open(mp, "w")); m.MANIFEST = mp
with zipfile.ZipFile(sys.argv[2], "w") as zf: zf.writestr("agent/arm64/swhid.sys", data)
sys.argv = ["fill", "--zip", sys.argv[2], "--out", os.path.join(sys.argv[3], "heg2")]
try: m.main()
except SystemExit as e: print("EXIT", e.code)
'''
    z2 = os.path.join(tmp, "windows-switch-arm64-drivers-v0_8_0.zip")
    p = subprocess.run([sys.executable, "-I", "-c", code, os.path.join(TOOLS, "fill_heg001.py"), z2, tmp], capture_output=True, text=True)
    check("fill: a file that matches the manifest is written", "EXIT 0" in p.stdout and open(os.path.join(tmp, "heg2/agent/arm64/swhid.sys"), "rb").read() == b"synthetic swhid", p.stdout + p.stderr)
finally:
    shutil.rmtree(tmp, ignore_errors=True)
print("\n%d failed" % len(fails) if fails else "\nall passed"); sys.exit(1 if fails else 0)
