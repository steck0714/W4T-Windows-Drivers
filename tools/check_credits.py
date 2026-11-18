#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Fails if a driver source file (.c/.cpp/.h under drivers/t210-icosa, drivers/t214-hoag, drivers/t214-aula/drivers, outside
tests/tools/gpu/app) has no SPDX, Copyright or W4T-CREDIT line in its first 40 lines (the credit rule, CREDITS.md)."""
import os, re, sys
root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
bases = ["drivers/t210-icosa", "drivers/t214-hoag", "drivers/t214-aula/drivers"]
skip = {"tests", "app", "gpu", "TegraGpu", "tools"}
bad = []; n = 0
for b in bases:
    for d, dirs, fs in os.walk(os.path.join(root, b)):
        dirs[:] = [x for x in dirs if x not in skip]
        for f in fs:
            if not f.endswith((".c", ".cpp", ".h")): continue
            n += 1
            p = os.path.join(d, f)
            head = "\n".join(open(p, encoding="utf-8", errors="replace").read().splitlines()[:40])
            if not re.search(r"SPDX|Copyright|W4T-CREDIT", head, re.I): bad.append(os.path.relpath(p, root))
for p in bad: print("CREDIT: no credit/licence line:", p)
print("%d file(s) checked, %d without a credit line" % (n, len(bad))); sys.exit(1 if bad else 0)
