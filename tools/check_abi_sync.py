#!/usr/bin/env python3
"""Check that the TegraGpu ABI headers are byte-identical to the ones pinned from
ruje0504-2/mesa-switch-woa (src/nouveau/vulkan/nvkmd/windows/), and that they compile on their own.

  tools/check_abi_sync.py                      # compare against the pinned SHA-256 list below
  tools/check_abi_sync.py --upstream DIR       # compare against a mesa-switch-woa checkout instead
                                               # (DIR = its src/nouveau/vulkan/nvkmd/windows)
  tools/check_abi_sync.py --update-pins DIR    # print a new PINS block after you deliberately re-synced

Why: the KMD (TegraGpu/windows) and the NVK user-mode driver share these structs by value. A silent
size/flag drift gives a KMD that misreads UMD requests. The headers carry _Static_assert on every size,
so compiling them also checks the layout."""
import argparse, hashlib, pathlib, subprocess, sys, tempfile

UPSTREAM = "ruje0504-2/mesa-switch-woa 1dbc9e5"
HERE = pathlib.Path(__file__).resolve().parent.parent
KMD = HERE / "drivers/t210-icosa/drivers/TegraGpu/windows"
UMD = HERE / "drivers/t210-icosa/drivers/TegraGpu/tools/nvkmd_windows"
FILES = {  # file -> directory it must live in
    "adapter_abi.h": KMD, "allocation_abi.h": KMD, "submit_abi.h": KMD, "nvdec_abi.h": KMD,
    "kmt_abi.h": UMD,
}
# --- PINS (sha256 of the upstream file at the commit named in UPSTREAM) ---
PINS = {
    "adapter_abi.h": "9cf36e45dac585d0456acf63fb7c441e3be6eb0582dae3fd222263b28719b3ae",
    "allocation_abi.h": "860bbf9c80475c7a853d182cea53ef70a03ef9a5e662aa06c04cf9001aa31ecd",
    "kmt_abi.h": "23f93d58cde3f893463270d263ba07ef3ad5523dbf2692b93fd7b2a2a1255a41",
    "nvdec_abi.h": "4dc45cc8e525577f3684c2b96c42b4bbd6ad353f1bb054a83cc81a448ae95e39",
    "submit_abi.h": "9359c37d2474e67363d24f1f87573015b5cf73ed1d926f55aac7876071de5d9f",
}
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--upstream", type=pathlib.Path)
    ap.add_argument("--update-pins", type=pathlib.Path)
    a = ap.parse_args()
    if a.update_pins:
        print("PINS = {")
        for n in sorted(FILES): print(f'    "{n}": "{sha(a.update_pins / n)}",')
        print("}"); return 0
    bad = 0
    for n, d in FILES.items():
        p = d / n
        if not p.exists(): print(f"MISSING {p}"); bad += 1; continue
        want = sha(a.upstream / n) if a.upstream else PINS[n]
        if sha(p) != want:
            print(f"DRIFT   {p.relative_to(HERE)} differs from {UPSTREAM if not a.upstream else a.upstream}"); bad += 1
        else: print(f"same    {n}")
    # compile each header alone and all together (host compiler; headers only use <stdint.h>)
    src = "".join(f'#include "{n}"\n' for n in FILES if FILES[n] == KMD) + "int main(void){return 0;}\n"
    with tempfile.TemporaryDirectory() as t:
        c = pathlib.Path(t) / "abi.c"; c.write_text(src)
        r = subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(KMD), "-I", str(UMD),
                            "-fsyntax-only", str(c)], capture_output=True, text=True)
        if r.returncode: print("COMPILE FAIL\n" + r.stderr); bad += 1
        else: print("compile ok (all _Static_assert layouts hold)")
    return 1 if bad else 0
sys.exit(main())
