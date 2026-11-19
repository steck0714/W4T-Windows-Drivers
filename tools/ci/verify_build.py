#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Check a finished build/<MODEL>/drivers tree for the mistakes that make a driver silently useless on the console.

This is NOT a run on hardware. It catches build-output errors that a green compile does not:
  - a .sys that is not a valid PE file, is not ARM64 (machine 0xAA64), or is not a native/driver image
  - a .sys without an embedded signature (the kits are test-signed; an unsigned one will not load)
  - an INF that names a .sys / .cat that is not next to it, or that has no DriverVer / no ARM64 section
  - a kit folder with a .sys but no INF (nothing to install it with)
Exit code 0 = no problems, 1 = problems (listed), 2 = nothing to check.

  verify_build.py build/HAC-001/drivers [--allow-unsigned] [--no-inf-dir agent/arm64] [--no-cat-dir agent/wifi]
  verify_build.py --apps build/_apps/HAC-001      published apps: every top-level .exe must be an ARM64 image,
                                                  and no key material may be in the folder
"""
import argparse, os, re, struct, sys

IMAGE_FILE_MACHINE_ARM64 = 0xAA64
SUBSYSTEM_NATIVE = 1


def read_pe(path):
    """Returns dict(machine, subsystem, signed) or raises ValueError."""
    with open(path, "rb") as f:
        raw = f.read(1 << 20)
    if len(raw) < 0x40 or raw[:2] != b"MZ":
        raise ValueError("not a PE file (no MZ header)")
    (e_lfanew,) = struct.unpack_from("<I", raw, 0x3C)
    if e_lfanew < 0x40 or e_lfanew + 24 > len(raw) or raw[e_lfanew:e_lfanew + 4] != b"PE\0\0":
        raise ValueError("not a PE file (bad PE header offset)")
    (machine, _nsec, _ts, _sym, _nsym, opt_size, _chars) = struct.unpack_from("<HHIIIHH", raw, e_lfanew + 4)
    opt = e_lfanew + 24
    if opt + opt_size > len(raw) or opt_size < 0x58:
        raise ValueError("truncated optional header")
    (magic,) = struct.unpack_from("<H", raw, opt)
    if magic == 0x20B:      # PE32+
        dd_off, nrva_off = opt + 112, opt + 108
    elif magic == 0x10B:    # PE32
        dd_off, nrva_off = opt + 96, opt + 92
    else:
        raise ValueError("unknown optional header magic 0x%x" % magic)
    (subsystem,) = struct.unpack_from("<H", raw, opt + 68)
    (nrva,) = struct.unpack_from("<I", raw, nrva_off)
    signed = False
    if nrva > 4 and dd_off + 5 * 8 <= len(raw):
        (_va, size) = struct.unpack_from("<II", raw, dd_off + 4 * 8)   # IMAGE_DIRECTORY_ENTRY_SECURITY
        signed = size > 0
    return {"machine": machine, "subsystem": subsystem, "signed": signed}


def read_inf(path):
    raw = open(path, "rb").read()
    enc = "utf-16" if raw[:2] in (b"\xff\xfe", b"\xfe\xff") else "utf-8-sig"
    return raw.decode(enc, "replace")


def check_inf(path, folder, problems, skip_cat=False):
    text = read_inf(path)
    rel = os.path.relpath(path, folder)
    if not re.search(r"(?im)^\s*DriverVer\s*=\s*\S", text):
        problems.append("%s: no DriverVer" % rel)
    if not re.search(r"(?im)^\s*\[[^\]]*\bNT(?:ARM64|arm64)\b", text) and not re.search(r"(?i)NTarm64", text):
        problems.append("%s: no NTarm64 section (would not install on ARM64 Windows)" % rel)
    have = {n.lower() for n in os.listdir(os.path.dirname(path))}
    cat = re.search(r"(?im)^\s*CatalogFile(?:\.[A-Za-z0-9]+)?\s*=\s*([^\s;]+)", text)
    if cat and not skip_cat and cat.group(1).lower() not in have:
        problems.append("%s: CatalogFile %s is not next to the INF" % (rel, cat.group(1)))
    # files the INF copies by name ([SourceDisksFiles] lines are the authoritative list)
    sect = re.search(r"(?ims)^\s*\[SourceDisksFiles[^\]]*\]\s*$(.*?)(?=^\s*\[|\Z)", text)
    if sect:
        for line in sect.group(1).splitlines():
            line = line.split(";")[0].strip()
            m = re.match(r"([^\s=,]+\.(?:sys|dll|exe|bin|txt|hcd|cat))\s*=", line, re.I)
            if m and m.group(1).lower() not in have:
                problems.append("%s: lists %s in SourceDisksFiles but it is not in the folder" % (rel, m.group(1)))


FORBIDDEN_EXT = {".pfx", ".p12", ".pvk", ".key", ".pem", ".snk"}


def check_apps(folder):
    problems, n_exe = [], 0
    for app in sorted(os.listdir(folder)):
        d = os.path.join(folder, app)
        if not os.path.isdir(d):
            continue
        exe = os.path.join(d, app + ".exe")
        if not os.path.isfile(exe):
            problems.append("%s: %s.exe is missing" % (app, app))
        else:
            n_exe += 1
            try:
                pe = read_pe(exe)
                if pe["machine"] != IMAGE_FILE_MACHINE_ARM64:
                    problems.append("%s.exe: machine 0x%04x, not ARM64" % (app, pe["machine"]))
            except ValueError as e:
                problems.append("%s.exe: %s" % (app, e))
    for dp, _, fs in os.walk(folder):
        for f in fs:
            if os.path.splitext(f)[1].lower() in FORBIDDEN_EXT:
                problems.append("key material in the output: " + os.path.relpath(os.path.join(dp, f), folder))
    if n_exe == 0 and not problems:
        print("no apps in " + folder)
        return 2
    print("checked %d app(s) under %s" % (n_exe, folder))
    for p in problems:
        print("PROBLEM: " + p)
    print("OK" if not problems else "%d problem(s)" % len(problems))
    return 1 if problems else 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folder")
    ap.add_argument("--apps", action="store_true", help="check published apps (ARM64 .exe, no key files) instead of drivers")
    ap.add_argument("--allow-unsigned", action="store_true", help="do not require an embedded signature on .sys files")
    ap.add_argument("--no-cat-dir", action="append", default=[], metavar="REL_DIR",
                    help="folder (relative to FOLDER) whose INF has no .cat here because the Switch makes and signs the "
                         "catalog itself (windows-switch w64.cmd + Inf2Cat for Wi-Fi/Bluetooth/audio); repeatable")
    ap.add_argument("--no-inf-dir", action="append", default=[], metavar="REL_DIR",
                    help="folder (relative to FOLDER, e.g. agent/arm64) whose .sys files are installed without an INF "
                         "(windows-switch swhid/swbatt are registered by w64.cmd); repeatable")
    a = ap.parse_args(argv)
    folder = os.path.abspath(a.folder)
    if not os.path.isdir(folder):
        print("not a folder: " + folder)
        return 2
    if a.apps:
        return check_apps(folder)
    problems, n_sys, n_inf = [], 0, 0
    kits = {}
    no_cat = {os.path.normpath(os.path.join(folder, r)) for r in a.no_cat_dir}
    for d, _, fs in os.walk(folder):
        for f in fs:
            ext = os.path.splitext(f)[1].lower()
            p = os.path.join(d, f)
            if ext == ".sys":
                n_sys += 1
                kits.setdefault(d, {"sys": 0, "inf": 0})["sys"] += 1
                try:
                    pe = read_pe(p)
                except ValueError as e:
                    problems.append("%s: %s" % (os.path.relpath(p, folder), e))
                    continue
                if pe["machine"] != IMAGE_FILE_MACHINE_ARM64:
                    problems.append("%s: machine 0x%04x, not ARM64 (0xAA64)" % (os.path.relpath(p, folder), pe["machine"]))
                if pe["subsystem"] != SUBSYSTEM_NATIVE:
                    problems.append("%s: subsystem %d, a driver must be NATIVE (1)" % (os.path.relpath(p, folder), pe["subsystem"]))
                if not pe["signed"] and not a.allow_unsigned:
                    problems.append("%s: no embedded signature" % os.path.relpath(p, folder))
            elif ext == ".inf":
                n_inf += 1
                kits.setdefault(d, {"sys": 0, "inf": 0})["inf"] += 1
                try:
                    check_inf(p, folder, problems, skip_cat=os.path.normpath(d) in no_cat)
                except OSError as e:
                    problems.append("%s: %s" % (os.path.relpath(p, folder), e))
    no_inf = {os.path.normpath(os.path.join(folder, r)) for r in a.no_inf_dir}
    for d, c in kits.items():
        if c["sys"] and not c["inf"] and os.path.normpath(d) not in no_inf:
            problems.append("%s: has a .sys but no INF" % os.path.relpath(d, folder))
    if n_sys == 0 and n_inf == 0:
        print("nothing to check in " + folder)
        return 2
    print("checked %d .sys and %d .inf under %s" % (n_sys, n_inf, folder))
    for p in problems:
        print("PROBLEM: " + p)
    print("OK" if not problems else "%d problem(s)" % len(problems))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
