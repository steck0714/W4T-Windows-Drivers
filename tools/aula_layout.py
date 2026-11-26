#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""drivers/t214-aula keeps the OLED driver sources in one folder per driver (like t210-icosa / t214-hoag), but the
upstream build scripts and #includes expect DanilKhmyrov/windows-switch's own layout.

  aula_layout.py --check              prove, by SHA-256, that every moved file equals the upstream file after the W4T
                                      credit block and the listed include rewrites are taken out (exit 1 otherwise)
  aula_layout.py --materialize OUT    write a complete upstream-shaped tree (upstream-src/ plus the restored files) to OUT,
                                      ready for the upstream Docker / build.sh steps
"""
import argparse, hashlib, json, os, shutil, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BASE = os.path.join(ROOT, "drivers", "t214-aula")
BEGIN, END = "W4T-CREDIT-BEGIN", "W4T-CREDIT-END */\n"


def restore(entry):
    """Upstream bytes of one moved file, rebuilt from the repo copy. Raises ValueError on a malformed credit block."""
    data = open(os.path.join(BASE, entry["path"]), "rb").read()
    if entry["credit_block"]:
        t = data.decode("utf-8")
        if not t.startswith("/* " + BEGIN):
            raise ValueError("credit block missing at the top")
        i = t.find(END)
        if i < 0:
            raise ValueError("credit block not closed")
        t = t[i + len(END):]
        for new_text, up_text in entry["include_rewrites"]:
            if t.count(new_text) != 1:
                raise ValueError("include rewrite %r found %d times" % (new_text, t.count(new_text)))
            t = t.replace(new_text, up_text)
        data = t.encode("utf-8")
    return data


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--check", action="store_true")
    g.add_argument("--materialize", metavar="OUT")
    a = ap.parse_args(argv)
    lay = json.load(open(os.path.join(BASE, "layout.json"), encoding="utf-8"))
    bad = []
    restored = {}
    for e in lay["files"]:
        try:
            d = restore(e)
        except (OSError, ValueError) as ex:
            bad.append((e["path"], str(ex))); continue
        # an entry changed on purpose by this repo records w4t_modified_sha256 (see its w4t_modified_note) instead
        if hashlib.sha256(d).hexdigest() != e.get("w4t_modified_sha256", e["upstream_sha256"]):
            bad.append((e["path"], "restored bytes differ from the recorded SHA-256 (upstream, or w4t_modified_sha256)")); continue
        keep = os.path.join(BASE, "upstream-src", *e["upstream"].split("/"))
        # an untouched copy of the upstream file may stay in upstream-src/ (harmless; --materialize overwrites it with the restored bytes)
        if os.path.exists(keep) and hashlib.sha256(open(keep, "rb").read()).hexdigest() != e["upstream_sha256"]:
            bad.append((e["path"], "upstream-src/" + e["upstream"] + " exists but is not the untouched upstream file")); continue
        restored[e["upstream"]] = d
    if a.check:
        for p, why in bad: print("LAYOUT:", p, "-", why)
        print("%d moved file(s) checked, %d problem(s)" % (len(lay["files"]), len(bad)))
        return 1 if bad else 0
    if bad:
        for p, why in bad: print("LAYOUT:", p, "-", why)
        return 1
    out = os.path.abspath(a.materialize)
    if os.path.exists(out) and os.listdir(out):
        sys.exit("%s is not empty" % out)
    shutil.copytree(os.path.join(BASE, "upstream-src"), out, dirs_exist_ok=True)
    for up, d in restored.items():
        dest = os.path.join(out, *up.split("/"))
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        open(dest, "wb").write(d)
    print("wrote %d restored file(s) + upstream-src/ -> %s" % (len(restored), out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
