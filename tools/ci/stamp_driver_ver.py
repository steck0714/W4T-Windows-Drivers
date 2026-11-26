#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Make every CI build a driver UPDATE: stamp DriverVer in the INF files of the working copy (never committed by this script).

  stamp_driver_ver.py [--root drivers] [--run N] [--date MM/DD/YYYY] [--check]

Windows replaces an installed driver only when the new package has a higher DriverVer. The repository keeps a fixed
version a.b.c.d per INF; here the last field becomes d + N (N = the Actions run number, so it only ever grows) and the date
becomes today (UTC). Capped at 65535 (a DriverVer field limit). Skipped: upstream sources and prebuilt kit folders
('upstream', 'kit' path parts: their catalogs come from elsewhere). Prints one line per changed INF.
"""
import argparse, datetime, os, re, sys

PAT = re.compile(r"^(\s*DriverVer\s*=\s*)(\d{1,2}/\d{1,2}/\d{4})\s*,\s*(\d+)\.(\d+)\.(\d+)\.(\d+)(.*)$", re.I | re.M)
SKIP = {"upstream", "upstream-src", "kit", "kits", "node_modules", ".git"}


def stamp_text(text, run, date):
    def f(m):
        a, b, c, d = (int(m.group(i)) for i in (3, 4, 5, 6))
        return "%s%s,%d.%d.%d.%d%s" % (m.group(1), date, a, b, c, min(d + run, 65535), m.group(7))
    return PAT.sub(f, text)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default="drivers")
    ap.add_argument("--run", type=int, default=int(os.environ.get("GITHUB_RUN_NUMBER", "0") or 0))
    ap.add_argument("--date", default=datetime.datetime.now(datetime.timezone.utc).strftime("%m/%d/%Y"))
    ap.add_argument("--check", action="store_true", help="only report, change nothing")
    a = ap.parse_args(argv)
    n = 0
    for dp, dn, fn in os.walk(a.root):
        dn[:] = [d for d in dn if d.lower() not in SKIP]
        for name in fn:
            if not name.lower().endswith(".inf"):
                continue
            p = os.path.join(dp, name)
            raw = open(p, "rb").read()
            enc = "utf-16" if raw[:2] in (b"\xff\xfe", b"\xfe\xff") else "utf-8-sig" if raw[:3] == b"\xef\xbb\xbf" else "utf-8"
            try:
                text = raw.decode(enc)
            except UnicodeDecodeError:
                text = raw.decode("latin-1"); enc = "latin-1"
            new = stamp_text(text, a.run, a.date)
            if new != text:
                n += 1
                print("%s: %s" % (p, PAT.search(new).group(0).strip()))
                if not a.check:
                    nl = "\r\n" if "\r\n" in text else "\n"
                    open(p, "wb").write(new.encode(enc if enc != "utf-8-sig" else "utf-8-sig"))
    print("%d INF file(s) %s" % (n, "would change" if a.check else "stamped"))


if __name__ == "__main__":
    main()
