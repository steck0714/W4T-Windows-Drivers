#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Put the test-signing kit (tools/testsign/ + the build's public .cer) next to a model's drivers.

  make_testsign_kit.py --model HAC-001 --cer build/_work/HAC-001/kit/SwitchWOA-Test.cer [--out build] [--dest DIR]

Works for EVERY model in build/models.json (also those with build=none: scripts only, no .cer).
Private keys are refused. Output: <out>/<model>/test-signing/ (or --dest) + SHA256SUMS.txt inside it.
"""
import argparse, hashlib, json, os, shutil, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SRC = os.path.join(ROOT, "tools", "testsign")
FILES = ["Install-TestSigning.cmd", "Stage0-SwitchSetup.ps1", "Set-SwitchTestSigning.ps1", "README-ja.md"]
FORBIDDEN = {".pfx", ".p12", ".pvk", ".key", ".pem", ".snk"}


def looks_like_cert(path):
    raw = open(path, "rb").read(4096)
    return raw[:1] == b"\x30" or b"BEGIN CERTIFICATE" in raw   # DER (SEQUENCE) or PEM; private keys are refused by name above


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--model", required=True)
    ap.add_argument("--cer")
    ap.add_argument("--out", default=os.path.join(ROOT, "build"))
    ap.add_argument("--dest")
    a = ap.parse_args(argv)
    models = json.load(open(os.path.join(ROOT, "build", "models.json"), encoding="utf-8"))["models"]
    if a.model not in models:
        sys.exit("unknown model %r (have: %s)" % (a.model, ", ".join(models)))
    dest = os.path.abspath(a.dest) if a.dest else os.path.join(os.path.abspath(a.out), a.model, "test-signing")
    if a.cer:
        if os.path.splitext(a.cer)[1].lower() in FORBIDDEN:
            sys.exit("refusing: key material: " + a.cer)
        if not os.path.isfile(a.cer):
            sys.exit("certificate not found: " + a.cer)
        if os.path.splitext(a.cer)[1].lower() != ".cer" or not looks_like_cert(a.cer):
            sys.exit("not a public .cer: " + a.cer)
    shutil.rmtree(dest, ignore_errors=True)
    os.makedirs(dest)
    for f in FILES:
        shutil.copyfile(os.path.join(SRC, f), os.path.join(dest, f))
    if a.cer:
        shutil.copyfile(a.cer, os.path.join(dest, "SwitchWOA-Test.cer"))
    names = sorted(os.listdir(dest))
    with open(os.path.join(dest, "SHA256SUMS.txt"), "w") as out:
        for n in names:
            out.write("%s  %s\n" % (hashlib.sha256(open(os.path.join(dest, n), "rb").read()).hexdigest(), n))
    print("%s: test-signing kit (%s) -> %s" % (a.model, "with .cer" if a.cer else "scripts only, no .cer", dest))


if __name__ == "__main__":
    main()
