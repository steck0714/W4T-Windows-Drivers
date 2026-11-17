#!/usr/bin/env python3
"""Check reference/l4t: every file matches MANIFEST.sha256 and carries a license header.
  python3 tools/check_l4t_reference.py            verify
  python3 tools/check_l4t_reference.py --update   rewrite the manifest (after a deliberate upstream refresh)
"""
import hashlib, pathlib, sys
root = pathlib.Path(__file__).resolve().parent.parent / "reference/l4t"
skip = {"MANIFEST.sha256", "SOURCES.md", "NOTICE.md"}
files = sorted(p for p in root.rglob("*") if p.is_file() and p.name not in skip and "LICENSES" not in p.parts)
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
cur = {str(p.relative_to(root)): sha(p) for p in files}
mf = root / "MANIFEST.sha256"
if "--update" in sys.argv:
    mf.write_text("".join(f"{h}  {n}\n" for n, h in cur.items())); print("manifest rewritten:", len(cur)); sys.exit(0)
exp = dict(l.split("  ", 1)[::-1] for l in mf.read_text().splitlines())
exp = {n.strip(): h for n, h in exp.items()}
bad = [f"changed: {n}" for n in cur if n in exp and exp[n] != cur[n]]
bad += [f"not in manifest: {n}" for n in cur if n not in exp] + [f"missing: {n}" for n in exp if n not in cur]
for p in files:
    if p.name == "LICENSE": continue
    head = p.read_text(errors="replace")[:3000]
    if ("Permission is hereby granted" not in head and "General Public License" not in head
            and "SPDX-License-Identifier: GPL-2.0" not in head):
        bad.append(f"no license header: {p.relative_to(root)}")
for need in ("LICENSES/GPL-2.0.txt", "LICENSES/MIT-NVIDIA.txt"):
    if not (root / need).is_file(): bad.append(f"missing {need}")
print("\n".join(bad) or f"OK: {len(cur)} files verbatim, all with license headers")
sys.exit(1 if bad else 0)
