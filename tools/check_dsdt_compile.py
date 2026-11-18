#!/usr/bin/env python3
"""Compile acpi/Dsdt.winx-usually.asl with iasl for several macro sets.

Needs `iasl` (ACPICA) on PATH or in $IASL, plus gcc/cpp. Exit 77 = skipped (no iasl).
Platform.h (EDK2) is replaced by a stub that only defines the OEM revision; macro sets
mirror what NintendoSwitchPkg passes. This proves the ASL parses and the _LPI packages
are well formed, not that Windows accepts them.
"""
import os, pathlib, shutil, subprocess, sys, tempfile
root = pathlib.Path(__file__).resolve().parents[1]
iasl = os.environ.get("IASL") or shutil.which("iasl")
if not iasl:
    print("iasl not found; skipped"); sys.exit(77)
BASE = ["SWITCH_ACPI_SWOA_ENABLE", "SWITCH_ACPI_SWOA", "SWITCH_ERISTA_GPU"]
SETS = {
    "base": BASE,
    "lpi": BASE + ["SWITCH_LPI"],
    "lpi-c7-enabled": BASE + ["SWITCH_LPI", "SWITCH_LPI_C7_ENABLE"],
}
def run(cmd, cwd):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
rc = 0
with tempfile.TemporaryDirectory() as td:
    td = pathlib.Path(td)
    (td / "acpi").mkdir(); shutil.copytree(root / "acpi" / "t210", td / "acpi" / "t210")
    (td / "acpi" / "t214").mkdir()
    shutil.copy(root / "acpi" / "t214" / "Sio.asl", td / "acpi" / "t214")
    shutil.copy(root / "acpi" / "Dsdt.winx-usually.asl", td / "acpi")
    (td / "acpi" / "Platform.h").write_text("#define EFI_ACPI_ARM_OEM_REVISION 0x00000001\n")
    for name, defs in SETS.items():
        pre = td / f"{name}.i"; aml = td / f"{name}.asl"
        p = run(["gcc", "-E", "-x", "c", "-P", "-undef", "-nostdinc"] + [f"-D{d}" for d in defs] +
                ["-I", str(td / "acpi"), str(td / "acpi" / "Dsdt.winx-usually.asl"), "-o", str(pre)], td)
        if p.returncode: print(f"FAIL {name}: cpp\n{p.stderr[-600:]}"); rc = 1; continue
        aml.write_text(pre.read_text())
        p = run([iasl, "-p", str(td / name), str(aml)], td)
        ok = p.returncode == 0 and (td / f"{name}.aml").exists()
        n_lpi = pre.read_text().count("Method (_LPI")
        print(("PASS" if ok else "FAIL"), name, f"(_LPI methods: {n_lpi})")
        if not ok: print(p.stdout[-1200:], p.stderr[-400:]); rc = 1
        want = 4 if "SWITCH_LPI" in defs else 0
        if n_lpi != want: print(f"FAIL {name}: expected {want} _LPI, got {n_lpi}"); rc = 1
sys.exit(rc)
