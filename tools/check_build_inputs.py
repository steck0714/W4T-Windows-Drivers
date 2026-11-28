#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Static check of everything a CI driver build needs, BEFORE a Windows runner is used.

For every driver in tools/ci/drivers-*.json (the list tools/ci/Build-AllDrivers.ps1 builds) it checks:
  1. the driver directory, every source file and the INF exist;
  2. every quoted  #include "..."  (followed recursively) resolves to a file in this checkout, the family's
     tools folder, or a `repo_includes` root (external/mesa-switch-woa/tegra-x1-wddm, filled by CI);
     <system> includes are WDK/SDK headers and are not checked here;
  3. the INF's [SourceDisksFiles] / CopyFiles entries name the .sys that this driver produces or a file that
     is present next to the INF (firmware names are reported, never required: fwfetch loads them later);
  4. the kit folder named by the config exists under the family's kits folder;
  5. the pinned mesa-switch-woa revision (tools/ci/mesa-switch-woa.rev) is the one the workflows use, and the
     SwitchFan GPU bridge headers exist in a checkout of it (when --woa DIR is given or external/ is populated).

  tools/check_build_inputs.py [--model HAC-001|HDH-001|HEG-001 ...] [--woa DIR] [--strict]

Exit status 1 when something a build needs is missing. It does not compile anything: a pass means "no input is
missing", not "the driver builds" and not "the driver works on a console".
"""
import argparse, json, os, re, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
INC_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)
FW_EXT = {".bin", ".fw", ".nvm", ".clm_blob", ".txt"}


def rel(p):
    return os.path.relpath(p, ROOT).replace(os.sep, "/")


def read_text(p):
    raw = open(p, "rb").read()
    if raw[:2] in (b"\xff\xfe", b"\xfe\xff"):
        return raw.decode("utf-16", "replace")
    return raw.decode("utf-8", "replace")


def resolve(inc, cur_dir, roots):
    for base in [cur_dir] + roots:
        p = os.path.normpath(os.path.join(base, inc))
        if os.path.isfile(p):
            return p
    return None


def includes_with_depth(text):
    """(name, conditional) for each quoted include; conditional = inside any #if/#ifdef/#ifndef block."""
    depth, out = 0, []
    for line in text.splitlines():
        m = re.match(r"^\s*#\s*(if|ifdef|ifndef|endif)\b", line)
        if m:
            depth += -1 if m.group(1) == "endif" else 1
            depth = max(depth, 0)
            continue
        m = re.match(r'^\s*#\s*include\s+"([^"]+)"', line)
        if m:
            out.append((m.group(1), depth > 0))
    return out


def check_includes(src, roots, seen, problems, owner, notes=None):
    if src in seen:
        return
    seen.add(src)
    try:
        text = read_text(src)
    except OSError:
        return
    for inc, conditional in includes_with_depth(text):
        p = resolve(inc, os.path.dirname(src), roots)
        if p is None:
            msg = "%s: %s includes \"%s\" which was not found (searched: %s)" % (
                owner, rel(src), inc, ", ".join(rel(r) for r in [os.path.dirname(src)] + roots))
            if conditional and notes is not None:
                notes.append(msg + " [inside #if: only needed when that option is switched on]")
            else:
                problems.append(msg)
        else:
            check_includes(p, roots, seen, problems, owner, notes)


def inf_files(path):
    """File names named by [SourceDisksFiles*] and by CopyFiles-section lines."""
    text = read_text(path)
    sections, cur = {}, None
    for line in text.splitlines():
        line = line.split(";")[0].strip()
        if not line:
            continue
        m = re.match(r"^\[(.+)\]$", line)
        if m:
            cur = m.group(1).lower()
            sections.setdefault(cur, [])
        elif cur is not None:
            sections[cur].append(line)
    names = set()
    copy_secs = set()
    for sec, lines in sections.items():
        for l in lines:
            m = re.match(r"(?i)^copyfiles\s*=\s*(.+)$", l)
            if m:
                for part in m.group(1).split(","):
                    part = part.strip().lstrip("@")
                    if part.startswith("%") or part.startswith("*"):
                        continue
                    if "." in part and not part.lower().startswith("%"):
                        names.add(part)           # "@name.sys" or "name.sys" used directly
                    else:
                        copy_secs.add(part.lower())
    for sec in copy_secs:
        for l in sections.get(sec, []):
            names.add(l.split(",")[0].strip())
    for sec, lines in sections.items():
        if sec.startswith("sourcedisksfiles"):
            for l in lines:
                names.add(l.split("=")[0].strip())
    return {n for n in names if n and not n.startswith("%")}


def check_model(mid, m, woa, problems, notes):
    cfg = os.path.join(ROOT, m["ci_config"]) if m.get("ci_config") else None
    if m.get("source_dir") == "drivers/t214-aula":
        cfg = os.path.join(ROOT, "tools/ci/drivers-t214-aula.json")
    if not cfg or not os.path.isfile(cfg):
        notes.append("%s: no CI driver list (build=%s)" % (mid, m.get("build")))
        return
    driver_root = os.path.join(ROOT, m.get("driver_root") or m["source_dir"])
    tools_dir = os.path.join(ROOT, m.get("tools_dir") or os.path.join(m["source_dir"], "tools"))
    kits_dir = os.path.join(ROOT, m.get("kits_dir") or os.path.join(m["source_dir"], "kit"))
    for d in json.load(open(cfg, encoding="utf-8")):
        owner = "%s/%s" % (mid, d["name"])
        sink = problems if not d.get("optional") else notes
        local = []
        ddir = os.path.join(driver_root, d["dir"])
        if not os.path.isdir(ddir):
            local.append("%s: driver directory %s is missing" % (owner, rel(ddir)))
            sink.extend(local)
            continue
        roots = [ddir, os.path.join(ddir, "src"), tools_dir]
        for r in d.get("repo_includes") or []:
            rp = os.path.join(ROOT, r)
            if woa and r.startswith("external/mesa-switch-woa"):
                rp = os.path.join(woa, r.split("external/mesa-switch-woa/", 1)[1])
            roots.append(rp)
            if not os.path.isdir(rp):
                local.append("%s: repo_includes root %s is missing (CI fetches it; pass --woa DIR to check it here)" % (owner, r))
        for s in d["sources"]:
            sp = os.path.join(ddir, s)
            if not os.path.isfile(sp):
                local.append("%s: source %s is missing" % (owner, rel(sp)))
            else:
                check_includes(sp, roots, set(), local, owner, notes)
        inf = d.get("inf")
        if inf:
            ip = os.path.join(ddir, inf)
            if not os.path.isfile(ip):
                local.append("%s: INF %s is missing" % (owner, rel(ip)))
            else:
                built = {d["name"] + ".sys", os.path.splitext(os.path.basename(d["sources"][0]))[0] + ".sys"}
                for n in sorted(inf_files(ip)):
                    low = n.lower()
                    if low.endswith(".sys") and low in {b.lower() for b in built}:
                        continue
                    if low.endswith((".cat", ".inf")) or low == os.path.basename(ip).lower():
                        continue
                    if os.path.isfile(os.path.join(os.path.dirname(ip), n)):
                        continue
                    if os.path.splitext(low)[1] in FW_EXT:
                        notes.append("%s: INF names %s (firmware: loaded later by fwfetch, not a build input)" % (owner, n))
                        continue
                    if low.endswith(".sys"):
                        # the .sys name differs from the first source: accept when the INF's own driver name matches the config
                        local.append("%s: INF %s copies %s; the build produces %s" % (owner, rel(ip), n, "/".join(sorted(built))))
                    else:
                        local.append("%s: INF %s names %s which is not next to the INF" % (owner, rel(ip), n))
        kit = os.path.join(kits_dir, d["kit"])
        if not os.path.isdir(kit):
            notes.append("%s: kit folder %s does not exist (the kit is then made from the build output only)" % (owner, rel(kit)))
        sink.extend(local)


def check_woa(woa, problems, notes):
    revf = os.path.join(ROOT, "tools/ci/mesa-switch-woa.rev")
    rev = open(revf).read().strip()
    if not re.fullmatch(r"[0-9a-f]{40}", rev):
        problems.append("tools/ci/mesa-switch-woa.rev is not a full 40-hex commit: %r" % rev)
    wf_dir = os.path.join(ROOT, ".github/workflows")
    for f in sorted(os.listdir(wf_dir)):
        t = open(os.path.join(wf_dir, f), encoding="utf-8").read()
        for m in re.finditer(r"WOA_REV[A-Z_]*:\s*([0-9a-f]{40})", t):
            if m.group(1) != rev:
                problems.append("%s pins mesa-switch-woa %s but tools/ci/mesa-switch-woa.rev says %s" % (f, m.group(1), rev))
    base = woa or os.path.join(ROOT, "external/mesa-switch-woa/tegra-x1-wddm")
    if woa:
        base = os.path.join(woa, "tegra-x1-wddm")
    need = ["integration/switch-lite/windows/SwitchFan/src/t214/" + h for h in
            ("gpu_pmic.h", "emc_gpu_vote.h", "host1x_clock.h")] + ["windows/pmic.h"]
    if not os.path.isdir(base):
        notes.append("mesa-switch-woa checkout not present (CI fetches %s); pass --woa DIR to check the bridge headers" % rev[:8])
        return
    for n in need:
        if not os.path.isfile(os.path.join(base, n)):
            problems.append("mesa-switch-woa (%s) lacks %s: SwitchFan would fail with 'file not found'" % (base, n))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--model", action="append")
    ap.add_argument("--woa", help="checkout of ruje0504-2/mesa-switch-woa (the one whose HEAD is tools/ci/mesa-switch-woa.rev)")
    ap.add_argument("--strict", action="store_true", help="treat notes (optional drivers, missing kit folders) as errors too")
    a = ap.parse_args()
    models = json.load(open(os.path.join(ROOT, "build/models.json"), encoding="utf-8"))["models"]
    problems, notes = [], []
    for mid, m in models.items():
        if a.model and mid not in a.model:
            continue
        if m.get("build") != "ci":
            continue
        check_model(mid, m, a.woa, problems, notes)
    check_woa(a.woa, problems, notes)
    for n in notes:
        print("note: " + n)
    for p in problems:
        print("MISSING: " + p)
    print("%d problem(s), %d note(s)" % (len(problems), len(notes)))
    return 1 if problems or (a.strict and notes) else 0


if __name__ == "__main__":
    sys.exit(main())
