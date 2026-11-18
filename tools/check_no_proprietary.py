#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Fails if the repository tree contains a file that must not be redistributed (see tools/fwfetch/firmware-manifest.json).

Checks, for every file outside .git: (1) its SHA-256 against every published hash in the manifest,
(2) its name against the list of firmware names, (3) for 122368-byte files, the XUSB size from the manifest.
It cannot recognise files whose hash is unknown (bl31.bin, NVIDIA GPU blobs) except by NAME, so a renamed copy
would pass - this is a guard against accidents, not proof of absence.
"""
import hashlib, json, os, sys
root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
man = json.load(open(os.path.join(root, "tools/fwfetch/firmware-manifest.json")))
hashes, sizes, names = {}, {}, set()
for f in man["files"]:
    if "expect" in f:
        if "sha256" in f["expect"]: hashes[f["expect"]["sha256"]] = f["id"]
        if "size" in f["expect"] and "sha256" not in f["expect"]: sizes[f["expect"]["size"]] = f["id"]
    if "gpu" in f: names.add(f["gpu"].lower())
    for d in f.get("deploy", {}).get("windows-switch", []): names.add(os.path.basename(d).lower())
names |= {"tegra21x_xusb_firmware", "brcmfmac4356-pcie.bin", "brcmfmac4356-pcie.txt"}
# the driver sources legitimately mention these names in text; only flag files whose NAME is one of them
bad = []
for d, ds, fs in os.walk(root):
    ds[:] = [x for x in ds if x != ".git"]
    for fn in fs:
        p = os.path.join(d, fn); rel = os.path.relpath(p, root)
        if fn.lower() in names: bad.append((rel, "firmware file name")); continue
        try: sz = os.path.getsize(p)
        except OSError: continue
        if sz in sizes or sz > 30000:
            h = hashlib.sha256(open(p, "rb").read()).hexdigest()
            if h in hashes: bad.append((rel, "hash of " + hashes[h]))
            elif sz in sizes: bad.append((rel, "size of " + sizes[sz] + " (verify by hand)"))
# build/ may hold prebuilt drivers (allowed since 2026-11-18) but never key material
KEY_EXT = (".pfx", ".p12", ".pvk", ".key", ".pem", ".snk", ".keys")
for d, ds, fs in os.walk(os.path.join(root, "build")):
    for fn in fs:
        if fn.lower().endswith(KEY_EXT) or fn.lower() in ("prod.keys", "title.keys"):
            bad.append((os.path.relpath(os.path.join(d, fn), root), "private key material under build/"))
for rel, why in bad: print("PROPRIETARY?", rel, "-", why)
print("%d suspicious file(s)" % len(bad)); sys.exit(1 if bad else 0)
