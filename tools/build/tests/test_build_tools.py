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
    check("assemble: HEG-001 (OLED) is built by CI like the other models", rc == 0 and os.path.exists(os.path.join(out, "HEG-001", "MANIFEST.json")), log)
    # nested kit names (agent/arm64 ...) keep their layout under drivers/
    kit2 = os.path.join(tmp, "kit2"); os.makedirs(os.path.join(kit2, "agent", "arm64"))
    open(os.path.join(kit2, "agent", "arm64", "swhid.sys"), "wb").write(b"MZ" + b"\0" * 100)
    rc, log = run(os.path.join(TOOLS, "assemble_build.py"), "--model", "HEG-001", "--kit-dir", kit2, "--out", out)
    check("assemble: HEG-001 keeps agent/arm64/ under drivers/", rc == 0 and os.path.exists(os.path.join(out, "HEG-001", "drivers", "agent", "arm64", "swhid.sys")), log)
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
    # ---- make_testsign_kit.py: every model gets the kit, the .cer only when given, keys refused ----
    cer = os.path.join(tmp, "SwitchWOA-Test.cer"); open(cer, "wb").write(b"\x30\x82\x01\x00" + b"\0" * 64)
    tout = os.path.join(tmp, "ts")
    for mdl in ("HAC-001", "HAC-001-01", "HDH-001", "HEG-001"):
        rc, log = run(os.path.join(TOOLS, "make_testsign_kit.py"), "--model", mdl, "--cer", cer, "--out", tout)
        d = os.path.join(tout, mdl, "test-signing")
        check("testsign: %s gets scripts + .cer + SHA256SUMS" % mdl, rc == 0 and all(os.path.exists(os.path.join(d, n)) for n in ("Install-TestSigning.cmd", "Stage0-SwitchSetup.ps1", "Set-SwitchTestSigning.ps1", "SwitchWOA-Test.cer", "SHA256SUMS.txt")), log)
    rc, log = run(os.path.join(TOOLS, "make_testsign_kit.py"), "--model", "HAC-001-01", "--out", os.path.join(tmp, "ts2"))
    check("testsign: without --cer only scripts", rc == 0 and not os.path.exists(os.path.join(tmp, "ts2", "HAC-001-01", "test-signing", "SwitchWOA-Test.cer")), log)
    pfx = os.path.join(tmp, "k.pfx"); open(pfx, "wb").write(b"secret")
    rc, log = run(os.path.join(TOOLS, "make_testsign_kit.py"), "--model", "HAC-001", "--cer", pfx, "--out", tout)
    check("testsign: a private key is refused", rc != 0 and "key material" in log, log)
    rc, log = run(os.path.join(TOOLS, "make_testsign_kit.py"), "--model", "NOPE", "--out", tout)
    check("testsign: unknown model refused", rc != 0, log)
    # ---- sync_build.py: place CI artifacts into build/<model>/ (own folder, fake gh, no network) ----
    def mk_art(root, key=False):
        os.makedirs(os.path.join(root, "build-HAC-001", "drivers", "Switch-Fan"))
        open(os.path.join(root, "build-HAC-001", "drivers", "Switch-Fan", "switchfan.sys"), "wb").write(b"MZ-new")
        open(os.path.join(root, "build-HAC-001", "MANIFEST.json"), "w").write("{}")
        os.makedirs(os.path.join(root, "build-HDH-001", "drivers"))
        open(os.path.join(root, "build-HDH-001", "drivers", "lite.sys"), "wb").write(b"MZ-lite")
        os.makedirs(os.path.join(root, "apps-HAC-001", "SwitchCenter"))
        open(os.path.join(root, "apps-HAC-001", "SwitchCenter", "SwitchCenter.exe"), "wb").write(b"exe")
        if key:
            open(os.path.join(root, "build-HAC-001", "oops.pfx"), "wb").write(b"secret")
    art = os.path.join(tmp, "art"); mk_art(art)
    sout = os.path.join(tmp, "sync-out"); os.makedirs(os.path.join(sout, "HAC-001", "drivers"))
    open(os.path.join(sout, "HAC-001", "drivers", "stale.sys"), "wb").write(b"old")
    os.makedirs(os.path.join(sout, "HAC-001", "apps")); open(os.path.join(sout, "HAC-001", "apps", "keep.txt"), "w").write("k")
    rc, log = run(os.path.join(TOOLS, "sync_build.py"), "--artifacts-dir", art, "--models", "HAC-001", "--out", sout)
    check("sync: drivers placed, stale file gone, other model and apps left alone",
          rc == 0 and open(os.path.join(sout, "HAC-001", "drivers", "Switch-Fan", "switchfan.sys"), "rb").read() == b"MZ-new"
          and not os.path.exists(os.path.join(sout, "HAC-001", "drivers", "stale.sys"))
          and not os.path.exists(os.path.join(sout, "HDH-001")) and os.path.exists(os.path.join(sout, "HAC-001", "apps", "keep.txt"))
          and os.path.exists(os.path.join(sout, "HAC-001", "MANIFEST.json")), log)
    rc, log = run(os.path.join(TOOLS, "sync_build.py"), "--artifacts-dir", art, "--models", "all", "--apps", "--out", sout)
    check("sync: --apps replaces apps/, all models placed",
          rc == 0 and os.path.exists(os.path.join(sout, "HAC-001", "apps", "SwitchCenter", "SwitchCenter.exe"))
          and not os.path.exists(os.path.join(sout, "HAC-001", "apps", "keep.txt")) and os.path.exists(os.path.join(sout, "HDH-001", "drivers", "lite.sys")), log)
    bad = os.path.join(tmp, "art-key"); mk_art(bad, key=True)
    rc, log = run(os.path.join(TOOLS, "sync_build.py"), "--artifacts-dir", bad, "--out", os.path.join(tmp, "sync-bad"))
    check("sync: a private key in the artifacts is refused", rc != 0 and "private key" in log, log)
    rc, log = run(os.path.join(TOOLS, "sync_build.py"), "--artifacts-dir", art, "--models", "NOPE", "--out", sout)
    check("sync: unknown model refused", rc != 0 and "unknown model" in log, log)
    fake = os.path.join(tmp, "fakegh")
    open(fake, "w").write("#!%s\nimport json, os, sys\n"
        "a = sys.argv[1:]\n"
        "if a[:2] == ['run', 'list']:\n"
        "    print(json.dumps([] if os.environ.get('FAKE_NO_RUN') else [{'databaseId': 4242, 'headSha': 'abcdef0123456789', 'createdAt': 'now'}]))\n"
        "elif a[:3] == ['run', 'download', '4242']:\n"
        "    d = a[a.index('--dir') + 1]\n"
        "    for n in ('build-HDH-001',):\n"
        "        os.makedirs(os.path.join(d, n, 'drivers'))\n"
        "        open(os.path.join(d, n, 'drivers', 'lite.sys'), 'wb').write(b'from-gh')\n"
        "else:\n"
        "    sys.exit('unexpected: %%r' %% (a,))\n" % sys.executable)
    os.chmod(fake, 0o755)
    env = dict(os.environ, GH=fake)
    p = subprocess.run([sys.executable, "-I", os.path.join(TOOLS, "sync_build.py"), "--branch", "test", "--out", os.path.join(tmp, "sync-gh")], capture_output=True, text=True, env=env)
    check("sync: newest successful run is found and its artifact placed (gh)", p.returncode == 0 and "run 4242" in p.stdout
          and open(os.path.join(tmp, "sync-gh", "HDH-001", "drivers", "lite.sys"), "rb").read() == b"from-gh", p.stdout + p.stderr)
    env["FAKE_NO_RUN"] = "1"
    p = subprocess.run([sys.executable, "-I", os.path.join(TOOLS, "sync_build.py"), "--out", os.path.join(tmp, "sync-none")], capture_output=True, text=True, env=env)
    check("sync: no successful run gives a clear message", p.returncode != 0 and "no successful" in (p.stdout + p.stderr), p.stdout + p.stderr)
finally:
    shutil.rmtree(tmp, ignore_errors=True)
print("\n%d failed" % len(fails) if fails else "\nall passed"); sys.exit(1 if fails else 0)
