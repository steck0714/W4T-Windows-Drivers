#!/usr/bin/env python3
"""psci_t210.h constants must equal the pinned ATF copy in reference/atf/."""
import pathlib, re, sys
root = pathlib.Path(__file__).resolve().parents[5]
atf = (root / "reference/atf/tegra_def.h").read_text()
mine = (root / "drivers/t210-icosa/drivers/SwitchSleep/src/psci_t210.h").read_text()
pairs = {"CORE_POWERDN": "CORE_POWERDN", "CLUSTER_IDLE": "CLUSTER_IDLE", "SOC_POWERDN": "SOC_POWERDN"}
ok = True
for a, m in pairs.items():
    va = int(re.search(r"#define\s+PSTATE_ID_%s\s+U\((\d+)\)" % a, atf).group(1))
    vm = int(re.search(r"#define\s+T210_PSTATE_%s\s+(\d+)u" % m, mine).group(1))
    if va != vm: print(f"MISMATCH {a}: atf={va} header={vm}"); ok = False
mk = (root / "reference/atf/plat_psci_handlers.c").read_text()
ok &= "sc7entry_fw_base" in mk and "PSCI_E_NOT_SUPPORTED" in mk
print("psci ids match ATF copy" if ok else "FAIL"); sys.exit(0 if ok else 1)
