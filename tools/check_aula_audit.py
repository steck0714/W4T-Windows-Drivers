#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Fails if a file under drivers/t214-aula/upstream-src/ is not listed in drivers/t214-aula/LICENSE-AUDIT.md, or if a
binary/image/key slipped in (the tree is source-only; the few upstream images/binaries kept on purpose are pinned by SHA-256 in kept-binaries.json, built drivers by manifest/)."""
import os, sys, json, hashlib
root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
base = os.path.join(root, "drivers", "t214-aula")
kept = json.load(open(os.path.join(base, "kept-binaries.json"), encoding="utf-8"))["files"]  # binaries kept on purpose, pinned by SHA-256
audit = open(os.path.join(base, "LICENSE-AUDIT.md"), encoding="utf-8").read()
bad = []
for d, _, fs in os.walk(os.path.join(base, "upstream-src")):
    for f in fs:
        rel = os.path.relpath(os.path.join(d, f), os.path.join(base, "upstream-src")).replace(os.sep, "/")
        listed = ("`" + rel + "`" in audit) if not rel.startswith("patches/") else (f in audit)
        if not listed: bad.append((rel, "not in LICENSE-AUDIT.md"))
        if f.lower().endswith((".exe", ".sys", ".dll", ".bmp", ".jpg", ".png", ".pfx", ".key", ".pem", ".bin")):
            if f.lower().endswith((".pfx", ".key", ".pem")):
                bad.append((rel, "key/certificate private part is never allowed"))
            elif rel not in kept:
                bad.append((rel, "binary/image/key in a source-only tree (not in kept-binaries.json)"))
            elif hashlib.sha256(open(os.path.join(d, f), "rb").read()).hexdigest() != kept[rel]["sha256"]:
                bad.append((rel, "differs from the SHA-256 in kept-binaries.json"))
for rel, why in bad: print("AUDIT:", rel, "-", why)
# Files moved into per-driver folders (layout.json): still audited under their upstream path, and must restore byte-for-byte.
import json
lay = json.load(open(os.path.join(base, "layout.json"), encoding="utf-8"))
for e in lay["files"]:
    if "`" + e["upstream"] + "`" not in audit: bad.append((e["upstream"], "moved file not in LICENSE-AUDIT.md"))
    if not os.path.isfile(os.path.join(base, e["path"])): bad.append((e["path"], "listed in layout.json but missing"))
rc = os.system('"%s" -I "%s" --check >%s' % (sys.executable, os.path.join(root, "tools", "aula_layout.py"), os.devnull))
if rc != 0: bad.append(("layout.json", "tools/aula_layout.py --check failed (run it to see why)"))
print("%d problem(s)" % len(bad)); sys.exit(1 if bad else 0)
